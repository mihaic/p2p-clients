#include <dirent.h>
#include <iostream>
#include <fstream>
#include <iterator>
#include <exception>
#include <stdio.h>
#include <stdlib.h>

#include <stdlib.h>
#include <stdio.h>

#include <termios.h>
#include <string.h>
#include <sys/ioctl.h>

#include <sys/time.h>

#include <list>

// libtorrent needs boost, anyway
// so why not use it ourselves?
#include <boost/filesystem.hpp>
#include <boost/asio.hpp>

#include <libtorrent/alert_types.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/ip_filter.hpp>


float difftime( const struct timeval& from, const struct timeval& to ) {
    return (to.tv_sec - from.tv_sec) + ( to.tv_usec - from.tv_usec ) / 1000000.0f;
}

int main(int argc, char* argv[])
{


    using namespace libtorrent;
#if BOOST_VERSION < 103400
    namespace fs = boost::filesystem;
    fs::path::default_name_check(fs::no_check);
#endif

    //handles_t handles;
    struct timeval tstart, tend;
    gettimeofday( &tstart, NULL );
    if (argc < 2)
    {
        std::cerr << "usage: ./simple_client [-s:-o:-n:-d] torrent-file\n"
                                "\t\t-s  seeder: don't exit after download finishes\n"
                                "\t\t-o  download directory\n"
                                "\t\t-n  number of upload slots\n"
                                "\t\t-d  directory containing torrent files to seed\n"
                                "\t\t-q  use sequential piece picker\n"
                                ;
        return 1;
    }

    std::string save_path("./");
    std::list<std::string> torrents;
    bool seeder = false;
    int slots = 4;
    bool sequential = false;
    bool has_rate_limit = false;
    int rate_limit = -1;
    bool helper_mode = false;
    bool super_seeding = false;
    bool share_mode = false;
    bool fake_seeder = false;
    ip_filter ipf;

    for (int i=1; i<argc; ++i)
    {
        if (argv[i][0] != '-')
            torrents.push_back(std::string(argv[i]));
        else
        {
            switch (argv[i][1])
            {
                case 's':
                    std::cerr << "Seeding mode" << std::endl;
                    seeder = true;
                    break;
                case 'o':
                    i++;
                    if( i < argc )
                        save_path = argv[i];
                    else {
                        std::cerr << "-o requires an argument\n";
                        return -1;
                    }
                    break;
                case 'n':
                    i++;
                    if( i < argc )
                        slots = atoi(argv[i]);
                    else {
                        std::cerr << "-n requires an argument\n";
                        return -1;
                    }
                    break;
                case 'd':
                    i++;
                    if( i < argc ) {
                        DIR* d = opendir( argv[i] );
                        if( !d )
                            std::cerr << "Could not open dir " << argv[i] << "\n";
                        else {
                            struct dirent* e;
                            while( e = readdir( d ) ) {
                                if( (!strcmp( e->d_name, "." )) || (!strcmp( e->d_name, ".." )) )
                                    continue;
                                std::string p( argv[i] );
                                p = p + "/" + e->d_name;
                                torrents.push_back(p);
                            }
                            closedir( d );
                        }
                    }
                    else {
                        std::cerr << "-d requires an argument\n";
                        return -1;
                    }
                    break;
                case 'q':
                    sequential = true;
                    break;
                case 'h':
                    std::cerr << "Disconnecting seeders" << std::endl;
                    helper_mode = true;
                    break;
                case 'u':
		    std::cerr << "Superseeding" << std::endl;
                    super_seeding = true;
                    break;
                case 'm':
                    std::cerr << "Share mode" << std::endl;
                    share_mode = true;
                    break;
                case 'f':
                    std::cerr << "Fake seed mode" << std::endl;
                    fake_seeder = true;
                    break;
                case 't':
                    i++;
		    int wave;
                    if( i < argc )
                        wave = atoi(argv[i]);
                    else {
                        std::cerr << "-t requires an argument\n";
                        return -1;
                    }
                    std::cerr << "Late leecher " << wave << std::endl;
                    break;
                case 'l':
		    has_rate_limit = true;
                    i++;
                    if( i < argc )
                        rate_limit = atoi(argv[i]);
                    else {
                        std::cerr << "-l requires an argument\n";
                        return -1;
                    }
                    break;
                default:
                    std::cerr << "Unknown option: " << argv[i] << "\n";
                    return -1;
            }
        }
    }

    if (save_path == "./")
        std::cerr << "Warning: missing download directory param\n";

    if( torrents.size() == 0 ) {
        std::cerr << "No torrents found\n";
        return -1;
    }

    session s;
    struct session_settings ss = s.settings();
    ss.allow_multiple_connections_per_ip = true;
    ss.active_downloads = -1;
    ss.active_seeds = -1;
    ss.active_limit = 100000;
    ss.share_mode_target = 1;
    if (has_rate_limit) {
        ss.upload_rate_limit = rate_limit;
        ss.local_upload_rate_limit = rate_limit;
    }
    ss.unchoke_slots_limit = -1; // instead of s.set_max_uploads(30);
    //ss.strict_super_seeding = super_seeding;
    //ss.active_tracker_limit = 100000;
    //ss.incoming_starts_queued_torrents = true;

    // Disable uTP
    ss.enable_outgoing_utp = false;
    ss.enable_incoming_utp = false;

    // Choose rarest pieces from the start
    ss.initial_picker_threshold = 0;

    // Disable hash checking
    ss.disable_hash_checks = true;

    s.set_settings( ss );
    if (has_rate_limit) {
	ss = s.settings();
	std::cerr << "Upload rate limit " << ss.upload_rate_limit << std::endl;
	std::cerr << "Local upload rate limit " << ss.local_upload_rate_limit
		<< std::endl;
    }
//    if (helper_mode) {
//        ipf = ip_filter();
//	ipf.add_rule(asio::ip::address::from_string("95.211.198.140"),
//			asio::ip::address::from_string("95.211.198.140"),
//			ip_filter::blocked);
//	s.set_ip_filter(ipf);
//    }
    //s.stop_lsd();
    s.stop_upnp();
    s.stop_natpmp();

    error_code ec;
    s.listen_on(std::make_pair(0, 0), ec);

    std::list<torrent_handle> handles;

    for( std::list<std::string>::iterator i = torrents.begin(); i != torrents.end(); i++ ) {
        std::string& torrent = *i;
        std::cerr << "Opening " << torrent << "\n";
        add_torrent_params p;
        p.save_path = save_path;

	// Discard data
	p.storage = disabled_storage_constructor;

	if (share_mode) {
	    p.flags |= add_torrent_params::flag_share_mode;
	} else {
	    if (seeder && !fake_seeder) {
	        p.flags |= add_torrent_params::flag_seed_mode;
	    }
	}

        p.ti = new torrent_info(torrent, ec);
        if (ec)
        {
            std::cerr << "Could not create torrent_info" << std::endl;
            std::cerr << ec.message() << std::endl;
            return 1;
        }
        torrent_handle h = s.add_torrent(p, ec);
        // set sequential dl
        if( sequential )
            h.set_sequential_download(true);
        if (ec)
        {
            std::cerr << "Could not add torrent" << std::endl;
            std::cerr << ec.message() << std::endl;
            return 1;
        }
	if (super_seeding) {
	    h.super_seeding(super_seeding);
	}
        handles.push_back(h);
    }

    //session_status sess_stat = s.status();
    //torrent_status ts = h.status();
    //main loop... print usefull data logs
    long int down = 0;
    long int up = 0;
    float progress = 0.0;
    fprintf(stderr,"time\t%%complete\tup B\tdown B\n");
    bool first_piece_downloaded = false; //FIXME only works for one torrent
    torrent_status ts;
    //s.set_alert_mask(alert::all_categories);
    while (true)
    {
	while (true)
	{
	    std::auto_ptr<alert> a = s.pop_alert();
	    if (a.get() == NULL)
	    {
		    break;
	    }
	    std::cerr << a->message() << "\n";
	}
        gettimeofday(&tend, NULL);
        long int newup = s.status().total_payload_upload;
        long int newdown = s.status().total_payload_download;
        up = newup - up;
        down = newdown - down;
        bool allComplete = true;
        progress = 0.0;
        for( std::list<torrent_handle>::iterator i = handles.begin(); i != handles.end(); i++ ) {
            torrent_handle& h = *i;
	    ts = h.status();
            progress += ts.progress_ppm;
            if(ts.state != 5)
                allComplete = false;
            // SCHAAP: If you want quite extended info on your torrents...
            /*
            std::cerr << "Data on torrent " << h.name() << std::endl;
            const std::vector<announce_entry> trackers = h.trackers();
            for( std::vector<announce_entry>::const_iterator it = trackers.begin(); it != trackers.end(); it++ ) {
                announce_entry ae = *it;
                std::cerr << "-- Tracker " << ae.url << " : ";
                if( ae.updating ) {
                    if( ae.verified )
                        std::cerr << "(waiting for response, verified)";
                    else
                        std::cerr << "(waiting for response)";
                }
                else {
                    if( ae.verified )
                        std::cerr << "(verified)";
                    else
                        std::cerr << "(unknown)";
                }
                std::cerr << " - failed " << ae.fails << " times - last_error = " << ae.last_error << " - message = " << ae.message << std::endl;
            }
	    */
//	    if (helper_mode) {
//		std::vector<peer_info> v_pe;
//		ipf = s.get_ip_filter();
//                h.get_peer_info( v_pe );
//		std::cerr << "Peers\n";
//		for( std::vector<peer_info>::iterator it = v_pe.begin(); it !=
//				v_pe.end(); it++ ) {
//                    peer_info& pe = *it;
//		    std::cerr << pe.pid << " " << pe.progress << "\n";
//                    if( ipf.access(pe.ip.address()) != 0 ) {
//			std::cerr << "-- should not be connected to " <<
//				pe.ip.address() << "\n";
//			ipf.add_rule(pe.ip.address(), pe.ip.address(),
//					ip_filter::blocked);
//			s.set_ip_filter(ipf);
//		    }
//		}
//	    }
                /*else
                    std::cerr << "-- Peer: ";
                std::cerr << pe.ip.address().to_string() << ":" << pe.ip.port() << " - queue req down/up " << pe.download_queue_length << "/" << pe.upload_queue_length << " - failed " << pe.failcount << " - has " << pe.num_pieces << " pieces" << std::endl;
            }
            std::cerr << "/Data on torrent" << std::endl;
            */
        }
        progress /= handles.size();

        float dtime = difftime(tstart, tend);
        // %f for time, since 100 times "2 seconds" is not very useful
        // %0.2f for progress, since sub-percentage progress is available and hence more accurate; no more precision than 2 since at that point libtorrent itself
        //       doesn't seem that accurate (without actual progress the progress alternatingly increased and decreased less than 0.01 percent)
        // %li for up and down since that's in bytes
        fprintf(stderr,"%f\t%0.2f\t%li\t%li\n",
         dtime,
         (float)(progress / 10000.f),
         (long int)up,
         (long int)down);

	if (!first_piece_downloaded && ts.num_pieces > 0) {
	        first_piece_downloaded = true;
	        std::cerr << "First " << dtime << "\n";
	}
	std::cerr << "Transfer: " << dtime << " " << newup << " " << newdown <<
		std::endl;

        up = newup;
        down = newdown;

        // check if complete
        if (allComplete && !seeder)
            return 0;
        // This sleep is actually not the usual Linux sleep, but the one defined in libtorrent, which takes usecs
        // Going for 999 usec to compensate a little for execution time.
        sleep(999);
    }

    return 0;
}
