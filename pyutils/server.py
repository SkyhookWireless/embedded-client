#!/usr/bin/env python
import re
import socketserver
import threading
import logging.config
import elg_proto
import yaml
from xml.etree import ElementTree as ET
from io import BytesIO
import urllib.request
from http import HTTPStatus

def get_config():
    with open("server.yaml") as f:
        config = yaml.load(f)

    return config


def get_partner_keys():
    with open("partner_keys.yaml") as f:
        partner_keys = yaml.load(f)

    return partner_keys 


class RequestHandler(socketserver.BaseRequestHandler):
    request_count = 0

    def forward_rq_to_api_server(self, rq, api_key):
        result = None
        error_msg = None

        try:
            root = ET.Element('LocationRQ')
            root.set('xmlns:xsi', 'http://www.w3.org/2001/XMLSchema-instance')
            root.set('xsi:schemaLocation', 'http://skyhookwireless.com/wps/2005 ../../src/xsd/location.xsd')
            root.set('xmlns', 'http://skyhookwireless.com/wps/2005')
            root.set('version', '2.25')

            auth = ET.SubElement(root, 'authentication')
            auth.set('version', '2.2')

            key = ET.SubElement(auth, 'key')
            key.set('key', api_key)
            key.set('username', 'elg')

            # AP scans.
            if rq.aps:
                aps = list(zip(rq.aps.mac, rq.aps.rssi, rq.aps.band))

                for ap in aps:
                    ap_elem = ET.SubElement(root, 'access-point')

                    ET.SubElement(ap_elem, 'mac').text = format(ap[0], 'x')
                    ET.SubElement(ap_elem, 'signal-strength').text = str(ap[1])

            et = ET.ElementTree(root)
            body = BytesIO()
            et.write(body, encoding='utf-8', xml_declaration=True) 

            req = urllib.request.Request(self.config["api_url"],
                body.getvalue(), {'Content-Type': 'text/xml'})

            with urllib.request.urlopen(req) as resp:
                resp_xml = resp.read().decode('utf-8').strip()

            # Parse the XML response.
            #
            # Get the XML root after stripping off pesky namespace attribute.
            resp_root = ET.fromstring(re.sub(' xmlns="[^"]+"', '', resp_xml, count=1))

            location = resp_root.find('location')

            # FIXME: populate and return a protobuf RS message (not a "result" dict).
            result = {}

            result['lat'] = location.find('latitude').text
            result['lon'] = location.find('longitude').text
            result['hpe'] = location.find('hpe').text

        except Exception as e:
            raise

        return result 


    def handle(self):
        RequestHandler.request_count += 1 # GIL obviates need to use a lock.

        logger = logging.getLogger('handler')

        logger.info("Handling request {}. Active thread count = {}".format(RequestHandler.request_count, threading.active_count()))

        self.request.settimeout(self.config['conn_timeout'])

        try:
            # Read the header in order to get the partner_id and to determine
            # how long the message is.
            buf = bytearray()

            while len(buf) < elg_proto.RQ_HEADER_LEN:
                buf.extend(self.request.recv(elg_proto.RQ_HEADER_LEN - len(buf)))

            header = elg_proto.decode_rq_header(buf)

            logger.debug("---- header: ----\n" + str(header))

            # Get the AES and API keys for this customer.
            try:
                keys = self.partner_keys[header.partner_id]['keys']
            except KeyError:
                logger.warning("partner id {} not found in key file. Ignoring request".format(header.partner_id))
                return

            # Read the rest of the message.
            buf = bytearray()

            while len(buf) < header.remaining_length:
                buf.extend(self.request.recv(header.remaining_length - len(buf)))

            logger.debug("remaining bytes read: {}".format(len(buf)))

            key = bytearray.fromhex(keys['aes'])

            # Read and decode the remainder of the message.
            body = elg_proto.decode_rq_crypto_info_and_body(buf, key)

            logger.debug("---- body: ----\n" + str(body))

            rs = self.forward_rq_to_api_server(body, keys['api'])

            logger.debug("Response: " + str(rs))
        except Exception as e:
            logger.error("Error for partner ID {}: ".format(header.partner_id) +
                    type(e).__name__ + ':: ' + str(e))


if __name__ == "__main__":
    config = get_config()

    logging.config.dictConfig(config['log_config'])

    logging.info("Starting server...")

    partner_keys = get_partner_keys()

    RequestHandler.config = config
    RequestHandler.partner_keys = partner_keys

    server = socketserver.ThreadingTCPServer(("localhost", config['port']), RequestHandler)

    with server:
        listener_thread = threading.Thread(target=server.serve_forever)

        listener_thread.daemon = True
        listener_thread.start()

        server.allow_reuse_address = True
        server.serve_forever()
