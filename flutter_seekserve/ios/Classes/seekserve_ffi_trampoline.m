// Force the linker to keep SeekServe C API symbols alive.
//
// Problem: Dart FFI uses dlsym() at runtime, but the static linker strips
// symbols that have no compile-time references.
//
// Solution: This file is Objective-C so CocoaPods' -ObjC flag forces the
// linker to pull this .o file. The global array below holds real function
// pointers (address-of) that create relocations the compiler cannot elide.

#import <Foundation/Foundation.h>
#include <stdint.h>
#include <stdbool.h>

// Forward-declare the SeekServe C API (avoids header search-path issues).
typedef struct SeekServeEngine SeekServeEngine;

extern SeekServeEngine* ss_engine_create(const char* config_json);
extern void ss_engine_destroy(SeekServeEngine* engine);
extern int32_t ss_add_torrent(SeekServeEngine* engine, const char* uri,
                              char* out_torrent_id, int32_t out_torrent_id_len);
extern int32_t ss_remove_torrent(SeekServeEngine* engine, const char* torrent_id,
                                 _Bool delete_files);
extern int32_t ss_list_files(SeekServeEngine* engine, const char* torrent_id,
                             char** out_json);
extern int32_t ss_select_file(SeekServeEngine* engine, const char* torrent_id,
                              int32_t file_index);
extern int32_t ss_get_stream_url(SeekServeEngine* engine, const char* torrent_id,
                                 int32_t file_index, char** out_url);
extern int32_t ss_get_status(SeekServeEngine* engine, const char* torrent_id,
                             char** out_json);
extern int32_t ss_set_event_callback(SeekServeEngine* engine,
                                     void (*callback)(const char*, void*),
                                     void* user_data);
extern int32_t ss_start_server(SeekServeEngine* engine, uint16_t port,
                               uint16_t* out_port);
extern int32_t ss_stop_server(SeekServeEngine* engine);
extern void ss_free_string(char* str);

// Global array of function pointers — __attribute__((used)) prevents the
// compiler from dead-stripping this data, and the address-of (&) creates
// real relocations that force the linker to resolve every ss_* symbol.
__attribute__((used, visibility("default")))
void* _seekserve_force_link_symbols[] = {
    (void*)&ss_engine_create,
    (void*)&ss_engine_destroy,
    (void*)&ss_add_torrent,
    (void*)&ss_remove_torrent,
    (void*)&ss_list_files,
    (void*)&ss_select_file,
    (void*)&ss_get_stream_url,
    (void*)&ss_get_status,
    (void*)&ss_set_event_callback,
    (void*)&ss_start_server,
    (void*)&ss_stop_server,
    (void*)&ss_free_string,
};

// Dummy ObjC class — its mere existence causes -ObjC to pull this .o file.
@interface SeekServeFFIForceLink : NSObject
@end

@implementation SeekServeFFIForceLink
@end
