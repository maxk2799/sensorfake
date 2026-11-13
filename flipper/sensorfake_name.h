#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Name shown to apps while scanning, unless the name file says otherwise.
 * The ESP32 sketch's "Polar Sense 1234567" is too long for the Flipper. */
#define SENSORFAKE_NAME_DEFAULT "Polar Sense 1234"

/* The Flipper firmware has room for 16 characters in the advertised name */
#define SENSORFAKE_NAME_MAX_LENGTH 16

/** Turn the contents of the name file into a usable device name
 *
 * Takes the first line that has text on it, drops everything but plain ASCII
 * letters, digits, spaces and punctuation, trims spaces at both ends and cuts
 * the result to SENSORFAKE_NAME_MAX_LENGTH characters. Falls back to
 * SENSORFAKE_NAME_DEFAULT if nothing is left.
 *
 * @param text      file contents, not necessarily zero-terminated
 * @param text_len  length of text
 * @param name      output, at least SENSORFAKE_NAME_MAX_LENGTH + 1 bytes
 */
void sensorfake_name_parse(const char* text, size_t text_len, char* name);

#ifdef __cplusplus
}
#endif
