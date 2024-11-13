
#include "logger.h"

namespace dbg{

/**
 * @brief Prints the hexadecimal representation of an array of bytes.
 * 
 * This function prints the hexadecimal representation of an array of bytes, along with a tag and the length of the array.
 * Each byte is printed as "0xXX", separated by commas.
 * 
 * @param pary Pointer to the array of bytes.
 * @param len Length of the array.
 * @param tag Tag to be printed before the array.
 */
void hex_print(uint8_t *pary, uint16_t len, const char *tag){
    if(pary == NULL) return;
    log_w("%s [%d] bytes: [",tag, len);   
    for(uint16_t i = 0 ; i<len; i++)
    {
      log_i("0x%02x", *(uint8_t*)(pary + i));
      if(i != len-1) log_i(","); 
    }
    log_w("]\r\n"); 
}

// void hex_print(uint8_t *pary, uint16_t len, const char *tag){
//     if(pary == NULL) return;
//     log_w("%s[",tag);   
//     for(uint16_t i = 0 ; i<len-1; i++)
//     {
//       log_i("%02X", *(uint8_t*)(pary + i));
//       // if(i != len-1) log_i(","); 
//     }
//     log_w("%02X]\r\n", *(uint8_t*)(pary + len-1));
// }



} // namespace dbg










