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
#include <limits.h>
#include <stdbool.h>

#include "usbrelay.h"


const char *state_name(unsigned char state) {
   if (RELAY_ON == state) {
      return "on";
   }
   if (RELAY_OFF == state) {
      return "off";
   }
   return "[unknown code]";
}

int main(int argc, char *argv[]) {
   struct relay *relays = NULL;
   int exit_code = 0;

   if (argc > 1) {
      // first member is ignored everywhere
      relays = calloc(argc + 1, sizeof(struct relay));
      if (!relays) {
         fprintf(stderr, "couldn't allocate\n");
         return 2;
      }
   }

   /* loop through the command line and grab the relay details */
   for (int i = 1; i < argc; i++) {
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
         relays[i].state = RELAY_ON;
      } else {
         relays[i].state = RELAY_OFF;
      }
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
      fprintf(stderr, " - device:\n"
                    "           type: %04hx %04hx\n"
                    "           path: %s\n"
                    "  serial_number: %ls\n"
                    "   manufacturer: %ls\n"
                    "        product: %ls\n"
                    "        release: %hx\n"
                    "      interface: %d\n",
              cur_dev->vendor_id, cur_dev->product_id,
              cur_dev->path, cur_dev->serial_number,
              cur_dev->manufacturer_string,
              cur_dev->product_string,
              cur_dev->release_number,
              cur_dev->interface_number);

      int num_relays = 0;
      {
         // The product string is USBRelayx where x is number of relays read
         // comment not understood: "to the \0 in case there are more than 9"
         const wchar_t *prefix = L"USBRelay";
         const size_t prefix_len = wcslen(prefix);

         if (cur_dev->product_string
             && 0 == wcsncmp(cur_dev->product_string, prefix, prefix_len)
             && 0 != cur_dev->product_string[prefix_len]) {
            wchar_t *num_start = cur_dev->product_string + prefix_len;
            wchar_t *endptr = num_start + wcslen(num_start);
            const long read = wcstol(num_start, &endptr, 10);
            if (read > 1 && read <= RELAY_MAX) {
               num_relays = (int) read;
               fprintf(stderr, "    relay_count: %d (guessed based on product name)\n", num_relays);
            }
         }

         if (!num_relays) {
            num_relays = 2;
            fprintf(stderr, "    relay_count:  %d (couldn't extract from %ls, using default)\n",
                    num_relays, cur_dev->product_string);
         }
      }

      hid_device *handle = hid_open_path(cur_dev->path);
      if (!handle) {
         perror("unable to open device");
         return 1;
      }

      // 1 extra byte for the report ID
      unsigned char buf[9] = {0};
      buf[0] = 0x01;

      // this is documented to *not* overwrite the report id, and to start at buf[1]. However, it doesn't.
      int ret = hid_get_feature_report(handle, buf, sizeof(buf));
      if (ret == -1) {
         fprintf(stderr, "hid_get_feature_report failed: %ls\n", hid_error(handle));
         hid_close(handle);
         return 1;
      }

      buf[sizeof(buf) - 1] = '\0';

      if (!relays) {
         // we've not been asked to change anything, so just output the data
         for (int i = 0; i < num_relays; i++) {
            if (buf[7] & (1 << i)) {
               printf("%s_%d=1\n", buf, i + 1);
            } else {
               printf("%s_%d=0\n", buf, i + 1);
            }
         }
      }

      // loop through the supplied command line and try to match the serial
      for (int i = 1; i < argc; i++) {
         if (strcmp(relays[i].this_serial, (const char *) buf)) {
            continue;
         }

         if (operate_relay(handle, relays[i].relay_num, relays[i].state) < 0) {
            exit_code++;
         }

         relays[i].found = true;
      }

      hid_close(handle);
      fprintf(stderr, "\n");
      cur_dev = cur_dev->next;
   }

   hid_free_enumeration(devs);

   // Free static HIDAPI objects.
   hid_exit();

   for (int i = 1; i < argc; i++) {
      if (relays[i].found) {
         continue;
      }
      fprintf(stderr, "warning: unmatched request: %s, relay: %d, state: %s\n",
              relays[i].this_serial, relays[i].relay_num, state_name(relays[i].state));
      exit_code++;
   }

   free(relays);
   return exit_code;

   fail_to_parse:
   free(relays);
   return 2;
}

int operate_relay(hid_device *handle, unsigned char relay, unsigned char state) {
   const unsigned char report_number = 0x0;
   unsigned char buf[9] = {report_number, state, relay};
   const int res = hid_write(handle, buf, sizeof(buf));
   if (res < 0) {
      fprintf(stderr, "hid_write failed: %ls\n", hid_error(handle));
   }
   return (res);
}
