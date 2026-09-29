/*----------------------------------------------*/
/* TJpgDec System Configurations R0.03          */
/*----------------------------------------------*/
/*
 * The host build's configuration of ChaN's TJpgDec (tjpgd.c and tjpgd.h
 * beside this file, from Espressif's esp_jpeg 1.3.1, unmodified). Set as the
 * ESP32-S3's ROM copy is fixed (esp_jpeg's README, "Fixed compilation
 * configuration of the ROM code"), so the host decodes the way the board
 * does: a 512-byte input buffer, RGB888 out, descaling and the clip table
 * on, the basic decoder. Host only: no board image carries this code.
 */
#define JD_SZBUF           512
#define JD_FORMAT          0
#define JD_USE_SCALE       1
#define JD_TBLCLIP         1
#define JD_FASTDECODE      0
#define JD_DEFAULT_HUFFMAN 0
