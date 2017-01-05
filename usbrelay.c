/*
usbrelay: Control USB HID connected electrical relay modules
Copyright (C) 2014  Darryl Bond

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>
#include <hidapi/hidapi.h>
#include <limits.h>
#include "usbrelay.h"


const char *state_name(unsigned char state) {
   if (ON == state) {
      return "on";
   }
   if (OFF == state) {
      return "off";
   }
   return "[unknown code]";
}

int main(int argc, char *argv[]) {
   struct relay *relays = NULL;
   unsigned char buf[9]; // 1 extra byte for the report ID

   int debug = 0;
   int num_relays = 2;
   int i;
   int exit_code = 0;

   /* allocate the memory for all the relays */
   if (argc > 1) {
      /* Yeah, I know. Not using the first member */
      relays = calloc(argc + 1, sizeof(struct relay));
   } else {
      debug = 1;
   }

   /* loop through the command line and grab the relay details */
   for (i = 1; i < argc; i++) {
      /* copy the arg and bounds check */
      char arg_t[20] = {'\0'};
      strncpy(arg_t, argv[i], sizeof(arg_t) - 1);
      arg_t[sizeof(arg_t) - 1] = '\0';

      const char delimiters[] = "_=";

      char *token = strtok(arg_t, delimiters);
      if (token == NULL) {
         fprintf(stderr, "can't parse first part of argument: '%s'\n", arg_t);
         goto fail_to_parse;
      }
      strcpy(relays[i].this_serial, token);

      token = strtok(NULL, delimiters);
      if (token == NULL) {
         fprintf(stderr, "can't parse second part of argument: '%s'\n", arg_t);
         goto fail_to_parse;
      }

      const long seen = atol(token);
      if (seen < 0 || seen > UCHAR_MAX) {
         fprintf(stderr, "relay num must be less than %d (and probably a lot lower than that)\n", UCHAR_MAX);
         goto fail_to_parse;
      }
      relays[i].relay_num = (unsigned char) seen;

      token = strtok(NULL, delimiters);
      if (token == NULL) {
         fprintf(stderr, "can't parse second part of argument: '%s'\n", arg_t);
         goto fail_to_parse;
      }

      if (atol(token)) {
         relays[i].state = ON;
      } else {
         relays[i].state = OFF;
      }

      fprintf(stderr, "Orig: %s, Serial: %s, Relay: %d State: %s\n",
              arg_t,
              relays[i].this_serial, relays[i].relay_num, state_name(relays[i].state));
   }


   unsigned short vendor_id = 0x16c0;
   unsigned short product_id = 0x05df;

   const char *usb_id = getenv("USBID");
   if (usb_id != NULL) {
      char *split = strdup(usb_id);
      char *const orig = split;
      const char *vendor = strsep(&split, ":");

      if (!vendor || !*vendor || !split || !*split) {
         fprintf(stderr, "invalid format for USBID, expecting 'abcd:ef12': '%s'\n", usb_id);
         free(orig);
         goto fail_to_parse;
      }

      long first = strtol(vendor, NULL, 16);
      long second = strtol(split, NULL, 16);
      free(orig);

      if (first < 0 || first > USHRT_MAX || second < 0 || second > USHRT_MAX) {
         fprintf(stderr, "invalid USBID, numbers are out of range: '%s'\n", usb_id);
         goto fail_to_parse;
      }

      vendor_id = (unsigned short) first;
      product_id = (unsigned short) second;
   }

   struct hid_device_info *const devs = hid_enumerate(vendor_id, product_id);
   struct hid_device_info *cur_dev = devs;

   while (cur_dev) {
      fprintf(stderr, "Device Found\n  type: %04hx %04hx\n  path: %s\n  serial_number: %ls\n",
              cur_dev->vendor_id, cur_dev->product_id,
              cur_dev->path, cur_dev->serial_number);
      fprintf(stderr, "  Manufacturer: %ls\n", cur_dev->manufacturer_string);
      fprintf(stderr, "  Product:      %ls\n", cur_dev->product_string);
      fprintf(stderr, "  Release:      %hx\n", cur_dev->release_number);
      fprintf(stderr, "  Interface:    %d\n", cur_dev->interface_number);

      // The product string is USBRelayx where x is number of relays read to the \0 in case there are more than 9
      num_relays = atoi((const char *) &cur_dev->product_string[8]);
      fprintf(stderr, "  Number of Relays = %d\n", num_relays);

      hid_device *handle = hid_open_path(cur_dev->path);
      if (!handle) {
         fprintf(stderr, "unable to open device\n");
         return 1;
      }

      buf[0] = 0x01;
      int ret = hid_get_feature_report(handle, buf, sizeof(buf));
      if (ret == -1) {
         perror("hid_get_feature_report");
         hid_close(handle);
         return 1;
      }


      if (debug) {
         for (i = 0; i < num_relays; i++) {
            if (buf[7] & 1 << i) {
               printf("%s_%d=1\n", buf, i + 1);
            } else {
               printf("%s_%d=0\n", buf, i + 1);
            }
         }
      }

      /* loop through the supplied command line and try to match the serial */
      for (i = 1; i < argc; i++) {
         fprintf(stderr, "Serial: %s, Relay: %d State: %x \n", relays[i].this_serial, relays[i].relay_num,
                 relays[i].state);
         if (!strcmp(relays[i].this_serial, (const char *) buf)) {
            fprintf(stderr, "%d HID Serial: %s ", i, buf);
            fprintf(stderr, "Serial: %s, Relay: %d State: %x\n", relays[i].this_serial, relays[i].relay_num,
                    relays[i].state);
            if (operate_relay(handle, relays[i].relay_num, relays[i].state) < 0)
               exit_code++;
            relays[i].found = 1;
         }
      }
      hid_close(handle);
      fprintf(stderr, "\n");
      cur_dev = cur_dev->next;
   }
   hid_free_enumeration(devs);

   /* Free static HIDAPI objects. */
   hid_exit();

   for (i = 1; i < argc; i++) {
      fprintf(stderr, "Serial: %s, Relay: %d State: %x ", relays[i].this_serial, relays[i].relay_num, relays[i].state);
      if (relays[i].found)
         fprintf(stderr, "--- Found\n");
      else {
         fprintf(stderr, "--- Not Found\n");
         exit_code++;
      }
   }

   free(relays);
   return exit_code;

   fail_to_parse:
   free(relays);
   return 2;
}

int operate_relay(hid_device *handle, unsigned char relay, unsigned char state) {
   unsigned char buf[9];// 1 extra byte for the report ID
   int res;

   buf[0] = 0x0; //report number
   buf[1] = state;
   buf[2] = relay;
   buf[3] = 0x00;
   buf[4] = 0x00;
   buf[5] = 0x00;
   buf[6] = 0x00;
   buf[7] = 0x00;
   buf[8] = 0x00;
   res = hid_write(handle, buf, sizeof(buf));
   if (res < 0) {
      fprintf(stderr, "Unable to write()\n");
      fprintf(stderr, "Error: %ls\n", hid_error(handle));
   }
   return (res);
}
