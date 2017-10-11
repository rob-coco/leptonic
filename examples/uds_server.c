#include "log.h"
#include "vospi.h"
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCKET_PATH "/tmp/lepton.sock"

/**
 * Main entry point for example.
 *
 * This example creates a Unix Domain Socket server that listens for a connection on a socket (/tmp/lepton.sock).
 * Once a connection is created, the server streams base64-encoded frames from the Lepton to the client.
 */
int main(int argc, char *argv[])
{
	log_set_level(LOG_INFO);
	int fd, sock, client, written;
  struct sockaddr_un addr;

	// Remind the user about using this example after the telemetry ones
	log_info("Note that this example assumes the Lepton is in the default startup state.");
	log_info("If you've already run the telemetry examples, it likely won't work without a restart.");

  // Check we have enough arguments to work
  if (argc < 2) {
    log_error("Can't start - SPI device file path must be specified.");
    exit(-1);
  }

	// Open the spidev device
	log_info("opening SPI device...");
	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		log_fatal("SPI: failed to open device - check permissions & spidev enabled");
		exit(-1);
	}

	// Initialise the VoSPI interface
	if (vospi_init(fd, 20000000) == -1) {
			log_fatal("SPI: failed condition SPI device for VoSPI use.");
			exit(-1);
	}

  // Unlink the existing socket
  unlink(SOCKET_PATH);

  // Open the socket file
  sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0) {
  		log_fatal("Socket: failed to open socket");
  		exit(-1);
  }

  // Set up the socket
	log_info("creating socket...");
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
  bind(sock, (struct sockaddr*)&addr, sizeof(addr));

  // Start listening
  if (listen(sock, 5) == -1) {
    log_fatal("error listening on socket");
    exit(-1);
  }

	// Allocate space to receive the segments
	log_debug("allocating space for segments...");
	vospi_segment_t* segments[VOSPI_SEGMENTS_PER_FRAME];
	for (int seg = 0; seg < VOSPI_SEGMENTS_PER_FRAME; seg ++) {
		segments[seg] = malloc(sizeof(vospi_segment_t));
	}

	do {

    log_info("waiting for a connection...");
    if ((client = accept(sock, NULL, NULL)) == -1) {
      log_warn("error accepting connection from client");
      continue;
    }

  	do {

  		// Synchronise and transfer a single frame
  		log_info("aquiring VoSPI synchronisation");
  		if (0 == sync_and_transfer_frame(fd, segments, TELEMETRY_DISABLED)) {
  			log_error("failed to obtain frame from device.");
  	    exit(-10);
  		}
  		log_info("VoSPI stream synchronised");

  		do {
  				if (!transfer_frame(fd, segments, TELEMETRY_DISABLED)) {
  					break;
  				}


					// Read out all segments together rather than writing one at a time
					// Lags between frames is fine but lags between segments is not good
					for (int seg = 0; seg < VOSPI_SEGMENTS_PER_FRAME; seg ++) {
						if (segments[seg]->packets[20].id >> 12 != seg) {
							break;
						}
						log_info("Packet 20 ID: %d", segments[seg]->packets[20].id >> 12);
						for (int pkt = 0; pkt < VOSPI_PACKETS_PER_SEGMENT; pkt ++) {
							write(client, segments[seg]->packets[pkt].symbols, VOSPI_PACKET_SYMBOLS);
						}
					}

  		} while (1); // While we are synchronised

    } while (1);  // While we are connected to the client

	} while (1);  // Forever

	close(fd);
	return 0;
}
