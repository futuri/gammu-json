/**
 * gammu-json
 *
 * Copyright (c) 2013-2014 David Brown <hello at scri.pt>.
 * Copyright (c) 2013-2014 Medic Mobile, Inc. <david at medicmobile.org>
 *
 * All rights reserved.
 *
 * This is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version three,
 * as published by the Free Software Foundation.
 *
 * You should have received a copy of version three of the GNU General
 * Public License along with this software. If you did not, see
 * http://www.gnu.org/licenses/.
 *
 * This software is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL DAVID BROWN OR
 * MEDIC MOBILE BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <iconv.h>
#include <gammu.h>

#include "types.h"
#include "allocate.h"
#include "encoding.h"

/** --- **/

/**
 * @name utf16_surrogate_first:
 */
const uint16_t utf16_surrogate_first = 0xd800;

/**
 * @name utf16_surrogate_middle:
 */
const uint16_t utf16_surrogate_middle = 0xdc00;

/**
 * @name utf16_surrogate_last:
 */
const uint16_t utf16_surrogate_last = 0xdfff;

/**
 * @name utf16be_string_info:
 */
boolean_t utf16be_string_info(const char *s, string_info_t *i) {

  const char *p = s;
  boolean_t in_surrogate = FALSE;

  #define record_encoding_error(i, e) \
    do { \
      if ((i)->error == D_ERR_NONE) { \
        (i)->error = (e); \
        (i)->error_offset = (i)->bytes; \
      } \
    } while (0)

  i->bytes = 0;
  i->units = 0;
  i->symbols = 0;

  i->error_offset = 0;
  i->error = D_ERR_NONE;

  for (;;) {

    /* Terminator */
    if (p[0] == '\0') {
      break;
    }

    /* Partial character */
    if (p[1] == '\0') {
      record_encoding_error(i, D_ERR_PARTIAL_UNIT);
      i->bytes++;
      break;
    }

    /* Reassemble UTF-16 character */
    uint16_t v = (((uint16_t) p[0] << 8) | (uint16_t) p[1]);

    /* Handle surrogate pairs */
    if (!in_surrogate) {

      if (v < utf16_surrogate_first || v > utf16_surrogate_last) {
        /* Regular character */
        i->symbols++;
      } else {
        if (v < utf16_surrogate_middle) {
          /* Lead surrogate */
          in_surrogate = TRUE;
        } else {
          /* Unexpected trailing surrogate */
          record_encoding_error(i, D_ERR_UNEXPECTED_SURROGATE);
        }
      }

    } else {

      /* Surrogate pair will end */
      in_surrogate = FALSE;

      if (v >= utf16_surrogate_middle && v <= utf16_surrogate_last) {
        /* Trailing surrogate */
        i->symbols++;
      } else {
        /* Missing trailing surrogate */
        record_encoding_error(i, D_ERR_UNMATCHED_SURROGATE);
      }

    }

    /* Next */
    p += 2;
    i->units++;
    i->bytes += 2;
  }

  if (in_surrogate) {
    record_encoding_error(i, D_ERR_UNMATCHED_SURROGATE);
  }

  return TRUE;
};

/**
 * @name utf16be_encode_json_utf8:
 *   Copy and transform the string `s` to a newly-allocated
 *   buffer, making it suitable for output as a single utf-8
 *   JSON string. The caller must free the returned string.
 */
char *utf16be_encode_json_utf8(const char *s) {

  unsigned int i, j = 0;

  string_info_t si;
  utf16be_string_info(s, &si);

  /* Worst-case UTF-16-BE string allocation:
   *  Original length plus null terminator; two bytes for each
   *  character; every character escaped with a UTF-16 backslash. */

  char *b = allocate_array(2 * 2, si.units, 1);

  for (i = 0; i < si.units; ++i) {

    char msb = s[2 * i];
    char lsb = s[2 * i + 1];

    if (msb == '\0') {
      char escape = '\0';

      switch (lsb) {
        case '\r':
          escape = 'r'; break;
        case '\n':
          escape = 'n'; break;
        case '\f':
          escape = 'f'; break;
        case '\b':
          escape = 'b'; break;
        case '\t':
          escape = 't'; break;
        case '\\': case '"':
          escape = lsb; break;
        default:
          break;
      };

      if (escape != '\0') {
        b[j++] = '\0';
        b[j++] = '\\';
        lsb = escape;
      }
    }

    b[j++] = msb;
    b[j++] = lsb;
  }

  b[j++] = '\0';
  b[j++] = '\0';

  /* Worst-case UTF-8:
   *  Four bytes per character (see RFC3629) plus null terminator. */

  utf16be_string_info(b, &si);

  char *rv = allocate_array(4, si.units, 1);
  EncodeUTF8(rv, (uint8_t *) b);

  free(b);

  return rv;
}

/**
 * @name utf16be_is_gsm_codepoint:
 *   Given the most-significant byte `msb` and the least-significant
 *   byte `lsb` of a UCS-16-BE character, return TRUE if the character
 *   can be represented in the default GSM alphabet (described in GSM
 *   03.38). The GSM-to-Unicode conversion table used here was obtained
 *   from http://www.unicode.org/Public/MAPPINGS/ETSI/GSM0338.TXT.
 *
 *   Copyright (c) 2000 - 2009 Unicode, Inc. All Rights reserved.
 *   Unicode, Inc. hereby grants the right to freely use the information
 *   supplied in this file in the creation of products supporting the
 *   Unicode Standard, and to make copies of this file in any form for
 *   internal or external distribution as long as this notice remains
 *   attached.
 *  
 */
boolean_t utf16be_is_gsm_codepoint(uint8_t msb, uint8_t lsb) {

  switch (msb) {

    case 0x00: {
      int rv = (
        (lsb >= 0x20 && lsb <= 0x5f)
          || (lsb >= 0x61 && lsb <= 0x7e)
          || (lsb >= 0xa3 && lsb <= 0xa5)
          || (lsb >= 0xc4 && lsb <= 0xc6)
          || (lsb >= 0xe4 && lsb <= 0xe9)
      );
      if (rv) {
        return TRUE;
      }
      switch (lsb) {
        case 0x0a: case 0x0c: case 0x0d:
        case 0xa0: case 0xa1: case 0xa7:
        case 0xbf: case 0xc9: case 0xd1:
        case 0xd6: case 0xd8: case 0xdc:
        case 0xdf: case 0xe0: case 0xec:
        case 0xf1: case 0xf2: case 0xf6:
        case 0xf8: case 0xf9: case 0xfc:
          return TRUE;
        default:
          return FALSE;
      }
    }
    case 0x03: {
      switch (lsb) {
        case 0x93: case 0x94:
        case 0x98: case 0x9b:
        case 0x9e: case 0xa0:
        case 0xa3: case 0xa6:
        case 0xa8: case 0xa9:
          return TRUE;
        default:
          return FALSE;
      }
    }
    case 0x20: {
      return (lsb == 0xac);
    }
    default:
      break;
  }

  return FALSE;
}

/**
 * @name utf16be_is_gsm_string:
 *   Return true if the UCS-16-BE string `s` can be represented in
 *   the GSM default alphabet. The input string should be terminated
 *   by the UTF-16-BE null character (i.e. two null bytes).
 */
boolean_t utf16be_is_gsm_string(const char *s) {

  string_info_t si;
  utf16be_string_info(s, &si);

  for (size_t i = 0; i < si.units; ++i) {
    if (!utf16be_is_gsm_codepoint(s[2 * i], s[2 * i + 1])) {
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * @name utf8_string_info:
 */
boolean_t utf8_string_info(const char *str, string_info_t *i) {

  const char *p = str;
  size_t bytes_processed;

  i->units = 0;
  i->bytes = 0;
  i->symbols = 0;

  while (*p++) {

    if ((*p & 0xc0) != 0x80) {
      i->symbols++;
    }

    i->bytes++;
    i->units++;

    bytes_processed++;
  }

  return TRUE;
}

/* vim: set ts=4 sts=2 sw=2 expandtab: */