#ifdef USE_VST2
/// vst2_main.cpp
///
/// (c) 2018-2019 bsp. very loosely based on pongasoft's "hello, world" example plugin.
///
///   Licensed under the Apache License, Version 2.0 (the "License");
///   you may not use this file except in compliance with the License.
///   You may obtain a copy of the License at
///
///       http://www.apache.org/licenses/LICENSE-2.0
///
///   Unless required by applicable law or agreed to in writing, software
///   distributed under the License is distributed on an "AS IS" BASIS,
///   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
///   See the License for the specific language governing permissions and
///   limitations under the License.
///
/// created: 25Jun2018
/// changed: 26Jun2018, 27Jun2018, 29Jun2018, 01Jul2018, 02Jul2018, 06Jul2018, 13Jul2018
///          26Jul2018, 04Aug2018, 05Aug2018, 06Aug2018, 07Aug2018, 09Aug2018, 11Aug2018
///          18Aug2018, 19Aug2018, 05Sep2018, 06Sep2018, 10Oct2018, 26Oct2018, 10Mar2019
///          12Mar2019, 07May2019, 19May2019
///
///
///

// #define DEBUG_PRINT_EVENTS  defined
// #define DEBUG_PRINT_PARAMS  defined

#define NUM_INPUTS    (   8)  // must match AudioInterface.cpp:AUDIO_INPUTS
#define NUM_OUTPUTS   (   8)  // must match AudioInterface.cpp:AUDIO_OUTPUTS

// (note) causes reason to shut down when console is freed (when plugin is deleted)
// #define USE_CONSOLE  defined
// #define VST2_OPCODE_DEBUG  defined

#undef RACK_HOST

#define Dprintf if(0);else printf
// #define Dprintf if(1);else printf

// #define Dprintf_idle if(0);else printf
#define Dprintf_idle if(1);else printf

#include <aeffect.h>
#include <aeffectx.h>
#include <stdio.h>
#ifdef HAVE_UNIX
#include <unistd.h>
#endif

#include "../dep/yac/yac.h"
#include "../dep/yac/yac_host.cpp"
YAC_Host *yac_host;  // not actually used, just to satisfy the linker

#include "global_pre.hpp"
#include "global.hpp"
#include "global_ui.hpp"

#define EDITWIN_X 0
#define EDITWIN_Y 0
#define EDITWIN_W 1200
#define EDITWIN_H 800

#define Dfltequal(a, b)  ( (((a)-(b)) < 0.0f) ? (((a)-(b)) > -0.0001f) : (((a)-(b)) < 0.0001f) )

typedef union cmemptr_u {
   const sUI  *u32;
   const sF32 *f32;
   const void *any;
} cmemptr_t;

typedef union mem_u {
   sUI  u32;
   sF32 f32;
} mem_t;

extern int  vst2_init (int argc, char* argv[], bool _bFX);
extern void vst2_exit (void);
namespace rack {
extern void vst2_editor_redraw (void);
}
extern void vst2_set_samplerate (sF32 _rate);
extern void vst2_engine_process (float *const*_in, float **_out, unsigned int _numFrames);
extern void vst2_process_midi_input_event (sU8 _a, sU8 _b, sU8 _c);
extern void vst2_queue_param (int uniqueParamId, float value, bool bNormalized);
extern void vst2_handle_queued_params (void);
extern float vst2_get_param (int uniqueParamId);
extern void  vst2_get_param_name (int uniqueParamId, char *s, int sMaxLen);
extern void vst2_set_shared_plugin_tls_globals (void);  // see plugin.cpp
#ifdef USE_BEGIN_REDRAW_FXN
extern void vst2_begin_shared_plugin_redraw (void);  // see plugin.cpp
#endif // USE_BEGIN_REDRAW_FXN
extern "C" { extern int vst2_handle_effeditkeydown (unsigned int _vkey); }

namespace rack {
   extern bool b_touchkeyboard_enable;
   extern void settingsLoad(std::string filename, bool bWindowSizeOnly);
}


#include "../include/window.hpp"
#include "../dep/include/osdialog.h"
#include "../include/app.hpp"
#include <speex/speex_resampler.h>

// using namespace rack;
// extern void rack::windowRun(void);

#if defined(_WIN32) || defined(_WIN64)
#define HAVE_WINDOWS defined

#define WIN32_LEAN_AND_MEAN defined
#include <windows.h>
#include <xmmintrin.h>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;


// Windows:
#define VST_EXPORT  extern "C" __declspec(dllexport)


struct PluginMutex {
   CRITICAL_SECTION handle; 

   PluginMutex(void) {
      ::InitializeCriticalSection( &handle ); 
   }

   ~PluginMutex() {
      ::DeleteCriticalSection( &handle ); 
   }

   void lock(void) {
      ::EnterCriticalSection(&handle); 
   }

   void unlock(void) {
      ::LeaveCriticalSection(&handle); 
   }
};

#else

// MacOSX, Linux:
#define HAVE_UNIX defined

#define VST_EXPORT extern

#include <pthread.h> 
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <fenv.h>  // fesetround()
#include <stdarg.h>

// #define USE_LOG_PRINTF  defined

#ifdef USE_LOG_PRINTF
static FILE *logfile;
#undef Dprintf
#define Dprintf log_printf

void log_printf(const char *logData, ...) {
   static char buf[16*1024]; 
   va_list va; 
   va_start(va, logData); 
   vsprintf(buf, logData, va);
   va_end(va); 
   printf(buf); 
   fputs(buf, logfile);
   fflush(logfile);
}
#endif // USE_LOG_PRINTF


// #define _GNU_SOURCE
#include <dlfcn.h>

static pthread_mutex_t loc_pthread_mutex_t_init = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
//static pthread_mutex_t loc_pthread_mutex_t_init = PTHREAD_MUTEX_INITIALIZER;

struct PluginMutex {
   pthread_mutex_t handle;

   PluginMutex(void) {
      ::memcpy((void*)&handle, (const void*)&loc_pthread_mutex_t_init, sizeof(pthread_mutex_t));
   }

   ~PluginMutex() {
   }

   void lock(void) {
		::pthread_mutex_lock(&handle); 
   }

   void unlock(void) {
		::pthread_mutex_unlock(&handle); 
   }
};

#endif // _WIN32||_WIN64


// // extern "C" {
// // extern void glfwSetInstance(void *_glfw);
// // }



class PluginString : public YAC_String {
public:
   static const sUI QUOT2 =(sUI)(1<<26); // \'\'
   static const sUI STRFLQMASK  = (QUOT | UTAG1 | QUOT2);

   void           safeFreeChars      (void);
   sSI          _realloc             (sSI _numChars);
   sSI           lastIndexOf         (sChar _c, sUI _start) const;
   void          getDirName          (PluginString *_r) const;
   void          replace             (sChar _c, sChar _o);
};

void PluginString::safeFreeChars(void) {
   if(bflags & PluginString::DEL)
   {
      // if(!(bflags & PluginString::LA))
      {
         Dyacfreechars(chars);
      }
   }
}

sSI PluginString::_realloc(sSI _numBytes) { 

   // Force alloc if a very big string is about to shrink a lot or there is simply not enough space available
   if( ((buflen >= 1024) && ( (((sUI)_numBytes)<<3) < buflen )) || 
       (NULL == chars) || 
       (buflen < ((sUI)_numBytes))
       ) // xxx (!chars) hack added 180702 
   {
      if(NULL != chars) 
      { 
         sUI l = length; 

         if(((sUI)_numBytes) < l)
         {
            l = _numBytes;
         }

         sU8 *nc = Dyacallocchars(_numBytes + 1);
         sUI i = 0;

         for(; i<l; i++)
         {
            nc[i] = chars[i];
         }

         nc[i] = 0; 
 
         safeFreeChars();
		   buflen = (_numBytes + 1);
         bflags = PluginString::DEL | (bflags & PluginString::STRFLQMASK); // keep old stringflags
         length = i + 1;
         chars  = nc;
         key    = YAC_LOSTKEY;

         return YAC_TRUE;
      } 
      else 
      {
		   return PluginString::alloc(_numBytes + 1);
      }
   } 
	else 
	{ 
      key = YAC_LOSTKEY; // new 010208

		return YAC_TRUE;
	} 
}

sSI PluginString::lastIndexOf(sChar _c, sUI _start) const {
   sSI li = -1;

   if(NULL != chars)
   {
      sUI i = _start;

      for(; i<length; i++)
      {
         if(chars[i] == ((sChar)_c))
         {
            li = i;
         }
      }
   }

   return li;
}

void PluginString::replace(sChar _c, sChar _o) {
   if(NULL != chars)
   {
      for(sUI i = 0; i < length; i++)
      {
         if(chars[i] == _c)
            chars[i] = _o;
      }
   }
}

void PluginString::getDirName(PluginString *_r) const {
   sSI idxSlash     = lastIndexOf('/', 0);
   sSI idxBackSlash = lastIndexOf('\\', 0);
   sSI idxDrive     = lastIndexOf(':', 0);
   sSI idx = -1;

   if(idxSlash > idxBackSlash)
   {
      idx = idxSlash;
   }
   else
   {
      idx = idxBackSlash;
   }

   if(idxDrive > idx)
   {
      idx = idxDrive;
   }

   if(-1 != idx)
   {
      _r->_realloc(idx + 2);
      _r->length = idx + 2;

      sSI i;
      for(i=0; i<=idx; i++)
      {
         _r->chars[i] = chars[i];
      }

      _r->chars[i++] = 0;
      _r->key = YAC_LOSTKEY;
   }
   else
   {
      _r->empty();
   }
}

#define MAX_FLOATARRAYALLOCSIZE (1024*1024*64)

class PluginFloatArray : public YAC_FloatArray {
public:
   sSI  alloc          (sSI _maxelements);
};

sSI PluginFloatArray::alloc(sSI _max_elements) {
   if(((sUI)_max_elements)>MAX_FLOATARRAYALLOCSIZE)  
   {
      printf("[---] FloatArray::insane array size (maxelements=%08x)\n", _max_elements);
      return 0;
   }
   if(own_data) 
   {
      if(elements)  
      { 
         delete [] elements; 
         elements = NULL;
      } 
   }
   if(_max_elements)
   {
      elements = new(std::nothrow) sF32[_max_elements];
      if(elements)
      {
         max_elements = _max_elements;
         num_elements = 0; 
         own_data     = 1;
         return 1;
      }
   }
   num_elements = 0;
   max_elements = 0;
   return 0;
}



/*
 * I find the naming a bit confusing so I decided to use more meaningful names instead.
 */

/**
 * The VSTHostCallback is a function pointer so that the plugin can communicate with the host (not used in this small example)
 */
typedef audioMasterCallback VSTHostCallback;

/**
 * The VSTPlugin structure (AEffect) contains information about the plugin (like version, number of inputs, ...) and
 * callbacks so that the host can call the plugin to do its work. The primary callback will be `processReplacing` for
 * single precision (float) sample processing (or `processDoubleReplacing` for double precision (double)).
 */
typedef AEffect VSTPlugin;


// Since the host is expecting a very specific API we need to make sure it has C linkage (not C++)
extern "C" {

/*
 * This is the main entry point to the VST plugin.
 *
 * The host (DAW like Maschine, Ableton Live, Reason, ...) will look for this function with this exact API.
 *
 * It is the equivalent to `int main(int argc, char *argv[])` for a C executable.
 *
 * @param vstHostCallback is a callback so that the plugin can communicate with the host (not used in this small example)
 * @return a pointer to the AEffect structure
 */
VST_EXPORT VSTPlugin *VSTPluginMain (VSTHostCallback vstHostCallback);

// note this looks like this without the type aliases (and is obviously 100% equivalent)
// extern AEffect *VSTPluginMain(audioMasterCallback audioMaster);

}

/*
 * Constant for the version of the plugin. For example 1100 for version 1.1.0.0
 */
const VstInt32 PLUGIN_VERSION = 1000;


/**
 * Encapsulates the plugin as a C++ class. It will keep both the host callback and the structure required by the
 * host (VSTPlugin). This class will be stored in the `VSTPlugin.object` field (circular reference) so that it can
 * be accessed when the host calls the plugin back (for example in `processDoubleReplacing`).
 */
class VSTPluginWrapper {
public:
   static const uint32_t MIN_SAMPLE_RATE = 8192u;
   static const uint32_t MAX_SAMPLE_RATE = 384000u;
   static const uint32_t MIN_BLOCK_SIZE  = 64u;
   static const uint32_t MAX_BLOCK_SIZE  = 16384u;
   static const uint32_t MAX_OVERSAMPLE_FACTOR  = 16u;

   static const uint32_t IDLE_DETECT_NONE  = 0u;  // always active
   static const uint32_t IDLE_DETECT_MIDI  = 1u;  // become idle when output is silence, reactivate when there's MIDI input activity
   static const uint32_t IDLE_DETECT_AUDIO = 2u;  // become idle when output is silence, reactivate when there's audio input activity

public:
   rack::Global rack_global;
   rack::GlobalUI rack_global_ui;

protected:
   PluginString dllname;
   PluginString cwd;

public:
   struct {
      float factor;    // 1=no SR conversion, 2=oversample x2, 4=oversample x4, 0.5=undersample /2, ..
      int   quality;   // SPEEX_RESAMPLER_QUALITY_xxx
      float realtime_factor;   // used during realtime rendering
      int   realtime_quality;
      float offline_factor;    // used during offline rendering (bounce)
      int   offline_quality;   // 
      sUI   num_in;    // hack that limits oversampling to "n" input channels. default = NUM_INPUTS
      sUI   num_out;   // hack that limits oversampling to "n" input channels. default = NUM_OUTPUTS
      SpeexResamplerState *srs_in;
      SpeexResamplerState *srs_out;
      sF32 in_buffers[NUM_INPUTS * MAX_BLOCK_SIZE * MAX_OVERSAMPLE_FACTOR];
      sF32 out_buffers[NUM_OUTPUTS * MAX_BLOCK_SIZE];
   } oversample;

public:
   float    sample_rate;   // e.g. 44100.0

protected:
   uint32_t block_size;    // e.g. 64   

   PluginMutex mtx_audio;
public:
   PluginMutex mtx_mididev;

public:

   bool b_open;
   bool b_processing;  // true=generate output, false=suspended
   bool b_offline;  // true=offline rendering (HQ)
   bool b_check_offline;  // true=ask host if it's in offline rendering mode

   sUI  idle_detect_mode;
   sUI  idle_detect_mode_fx;
   sUI  idle_detect_mode_instr;
   sF32 idle_input_level_threshold;
   sF32 idle_output_level_threshold;
   sF32 idle_output_sec_threshold;
   sUI  idle_output_framecount;
   sF32 idle_noteon_sec_grace;  // grace period after note on
   sUI  idle_frames_since_noteon;

   bool b_idle;

   sBool b_fix_denorm;  // true=fix denormalized floats + clip to -4..4. fixes broken audio in FLStudio and Reason.

   ERect editor_rect;
   sBool b_editor_open;

   char *last_program_chunk_str;

   static sSI instance_count;
   sSI instance_id;

   sF32 tmp_input_buffers[NUM_INPUTS * MAX_BLOCK_SIZE];

   sUI redraw_ival_ms;  // 0=use DAW timer (effEditIdle)

public:
   VSTPluginWrapper(VSTHostCallback vstHostCallback,
                    VstInt32 vendorUniqueID,
                    VstInt32 vendorVersion,
                    VstInt32 numParams,
                    VstInt32 numPrograms,
                    VstInt32 numInputs,
                    VstInt32 numOutputs
                    );

   ~VSTPluginWrapper();

   VSTPlugin *getVSTPlugin(void) {
      return &_vstPlugin;
   }

   void setGlobals(void) {
      rack::global = &rack_global;
      rack::global_ui = &rack_global_ui;
   }

   sSI openEffect(void) {

      Dprintf("xxx vstrack_plugin::openEffect\n");

      // (todo) use mutex 
      instance_id = instance_count;
      Dprintf("xxx vstrack_plugin::openEffect: instance_id=%d\n", instance_id);

      rack_global.vst2.wrapper = this;

#ifdef USE_CONSOLE
      AllocConsole();
      freopen("CON", "w", stdout);
      freopen("CON", "w", stderr);
      freopen("CON", "r", stdin); // Note: "r", not "w".
#endif // USE_CONSOLE

      setGlobals();
      rack_global.init();
      rack_global_ui.init();
      rack::global->vst2.last_seen_instance_count = instance_count;

      char oldCWD[1024];
      char dllnameraw[1024];
      char *dllnamerawp = dllnameraw;

#ifdef HAVE_WINDOWS
      ::GetCurrentDirectory(1024, (LPSTR) oldCWD);
      // ::GetModuleFileNameA(NULL, dllnameraw, 1024); // returns executable name (not the dll pathname)
      GetModuleFileNameA((HINSTANCE)&__ImageBase, dllnameraw, 1024);
#elif defined(HAVE_UNIX)
      getcwd(oldCWD, 1024);
#if 0
      // this does not work, it reports the path of the host, not the plugin
      // (+the string is not NULL-terminated from the looks of it)
      readlink("/proc/self/exe", dllnameraw, 1024);
#else
      Dl_info dlInfo;
      ::dladdr((void*)VSTPluginMain, &dlInfo);
      // // dllnamerawp = (char*)dlInfo.dli_fname;
      if('/' != dlInfo.dli_fname[0])
      {
         // (note) 'dli_fname' can be a relative path (e.g. when loaded from vst2_debug_host)
         sprintf(dllnameraw, "%s/%s", oldCWD, dlInfo.dli_fname);
      }
      else
      {
         // Absolute path (e.g. when loaded from Renoise host)
         dllnamerawp = (char*)dlInfo.dli_fname;
      }
#endif
#endif

      Dprintf("xxx vstrack_plugin::openEffect: dllnamerawp=\"%s\"\n", dllnamerawp);
      dllname.visit(dllnamerawp);
      dllname.getDirName(&cwd);
      rack::global->vst2.program_dir = (const char*)cwd.chars;

      Dprintf("xxx vstrack_plugin::openEffect: cd to \"%s\"\n", (const char*)cwd.chars);
#ifdef HAVE_WINDOWS
      ::SetCurrentDirectory((const char*)cwd.chars);
#elif defined(HAVE_UNIX)
      chdir((const char*)cwd.chars);
#endif
      Dprintf("xxx vstrack_plugin::openEffect: cwd change done\n");
      // cwd.replace('\\', '/');

      int argc = 1;
      char *argv[1];
      //argv[0] = (char*)cwd.chars;
      argv[0] = (char*)dllnamerawp;
      Dprintf("xxx argv[0]=%p\n", argv[0]);
      Dprintf("xxx vstrack_plugin::openEffect: dllname=\"%s\"\n", argv[0]);
      (void)vst2_init(argc, argv,
#ifdef VST2_EFFECT
                      true/*bFX*/
#else
                      false/*bFX*/
#endif // VST2_EFFECT
                      );
      Dprintf("xxx vstrack_plugin::openEffect: vst2_init() done\n");

      vst2_set_shared_plugin_tls_globals();

      Dprintf("xxx vstrack_plugin::openEffect: restore cwd=\"%s\"\n", oldCWD);      

#ifdef HAVE_WINDOWS
      ::SetCurrentDirectory(oldCWD);
#elif defined(HAVE_UNIX)
      chdir(oldCWD);
#endif

      setSampleRate(sample_rate);

      b_open = true;
      b_editor_open = false;

      Dprintf("xxx vstrack_plugin::openEffect: LEAVE\n");
      return 1;
   }

   void setWindowSize(int _width, int _height) {
      if(_width < 640)
         _width = 640;
      if(_height < 480)
         _height = 480;

      editor_rect.right  = EDITWIN_X + _width;
      editor_rect.bottom = EDITWIN_Y + _height;

      (void)lglw_window_resize(rack_global_ui.window.lglw, _width, _height);
   }

   void setRefreshRate(float _hz) {
      if(_hz < 15.0f)
      {
         redraw_ival_ms = 0u;
      }
      else
      {
         redraw_ival_ms = sUI(1000.0f / _hz);
      }

      if(b_editor_open)
      {
         lglw_timer_stop(rack_global_ui.window.lglw);

         if(0u != redraw_ival_ms)
         {
            lglw_timer_start(rack_global_ui.window.lglw, redraw_ival_ms);
         }
      }
   }

   float getRefreshRate(void) {
      if(redraw_ival_ms > 0u)
         return (1000.0f / redraw_ival_ms);
      else
         return 0.0f;
   }

   void destroyResamplerStates(void) {
      if(NULL != oversample.srs_in)
      {
         speex_resampler_destroy(oversample.srs_in);
         oversample.srs_in = NULL;
      }

      if(NULL != oversample.srs_out)
      {
         speex_resampler_destroy(oversample.srs_out);
         oversample.srs_out = NULL;
      }
   }

   void openEditor(void *_hwnd) {
      Dprintf("xxx vstrack_plugin: openEditor() parentHWND=%p\n", _hwnd);
      setGlobals();
      (void)lglw_window_open(rack_global_ui.window.lglw,
                             _hwnd,
                             0/*x*/, 0/*y*/,
                             (editor_rect.right - editor_rect.left),
                             (editor_rect.bottom - editor_rect.top)
                             );

      if(0u != redraw_ival_ms)
      {
         lglw_timer_start(rack_global_ui.window.lglw, redraw_ival_ms);
      }

      b_editor_open = true;
      rack::global_ui->param_info.placeholder_framecount = (30*30)-10;
      rack::global_ui->param_info.last_param_widget = NULL;
   }

   void closeEditor(void) {
      Dprintf("xxx vstrack_plugin: closeEditor() b_editor_open=%d\n", b_editor_open);
      if(b_editor_open)
      {
         setGlobals();
         lglw_timer_stop(rack_global_ui.window.lglw);
         lglw_window_close(rack_global_ui.window.lglw);
         b_editor_open = false;
      }      
   }

   void closeEffect(void) {
      closeEditor();

      // (todo) use mutex
      Dprintf("xxx vstrack_plugin::closeEffect: last_program_chunk_str=%p\n", last_program_chunk_str);
      if(NULL != last_program_chunk_str)
      {
         ::free(last_program_chunk_str);
         last_program_chunk_str = NULL;
      }

      Dprintf("xxx vstrack_plugin::closeEffect: b_open=%d\n", b_open);

      if(b_open)
      {
         b_open = false;

         setGlobals();
         vst2_set_shared_plugin_tls_globals();
         rack::global->vst2.last_seen_instance_count = instance_count;

         Dprintf("xxx vstrack_plugin: call vst2_exit()\n");

         vst2_exit();

         Dprintf("xxx vstrack_plugin: vst2_exit() done\n");

         destroyResamplerStates();

         Dprintf("xxx vstrack_plugin: destroyResamplerStates() done\n");


#ifdef USE_CONSOLE
         Sleep(5000);
         // FreeConsole();
#endif // USE_CONSOLE
      }

   }

   void lockAudio(void) {
      mtx_audio.lock();
   }

   void unlockAudio(void) {
      mtx_audio.unlock();
   }

   VstInt32 getNumInputs(void) const {
      return _vstPlugin.numInputs;
   }

   VstInt32 getNumOutputs(void) const {
      return _vstPlugin.numOutputs;
   }

   void setOversample(float _factor, int _quality, bool _bLock = true) {

      oversample.factor  = _factor;
      oversample.quality = _quality;

      setSampleRate(sample_rate, _bLock);
   }

   void setOversampleRealtime(float _factor, int _quality) {
      Dprintf("xxx setOversampleRealtime 1\n");
      if(_factor < 0.0f)
         _factor = oversample.realtime_factor;  // keep

      if(_quality < 0)
         _quality = oversample.realtime_quality;  // keep

      if(_factor < 0.001f)
         _factor = 1.0f;
      else if(_factor > float(MAX_OVERSAMPLE_FACTOR))
         _factor = float(MAX_OVERSAMPLE_FACTOR);

      if(_quality < SPEEX_RESAMPLER_QUALITY_MIN/*0*/)
         _quality = SPEEX_RESAMPLER_QUALITY_MIN;
      else if(_quality > SPEEX_RESAMPLER_QUALITY_MAX/*10*/)
         _quality = SPEEX_RESAMPLER_QUALITY_MAX;

      oversample.realtime_factor = _factor;
      oversample.realtime_quality = _quality;

      Dprintf("xxx setOversampleRealtime 2 b_offline=%d\n", b_offline);

      if(!b_offline)
      {
         setOversample(oversample.realtime_factor, oversample.realtime_quality);
      }
   }

   void setOversampleOffline(float _factor, int _quality) {
      if(_factor < 0.0f)
         _factor = oversample.offline_factor;  // keep

      if(_quality < 0)
         _quality = oversample.offline_quality;  // keep

      if(_factor < 0.001f)
         _factor = 1.0f;
      else if(_factor > float(MAX_OVERSAMPLE_FACTOR))
         _factor = float(MAX_OVERSAMPLE_FACTOR);

      if(_quality < SPEEX_RESAMPLER_QUALITY_MIN/*0*/)
         _quality = SPEEX_RESAMPLER_QUALITY_MIN;
      else if(_quality > SPEEX_RESAMPLER_QUALITY_MAX/*10*/)
         _quality = SPEEX_RESAMPLER_QUALITY_MAX;

      oversample.offline_factor = _factor;
      oversample.offline_quality = _quality;

      if(b_offline)
      {
         setOversample(oversample.offline_factor, oversample.offline_quality);
      }
   }

   void setOversampleChannels(int _numIn, int _numOut) {
      if(_numIn < 0)
         _numIn = int(oversample.num_in);  // keep

      if(_numOut < 0)
         _numOut = int(oversample.num_out);  // keep

      if(_numIn < 0)
         _numIn = 0;
      else if(_numIn > NUM_INPUTS)
         _numIn = NUM_INPUTS;

      if(_numOut < 1)
         _numOut = 1;
      else if(_numOut > NUM_OUTPUTS)
         _numOut = NUM_OUTPUTS;

      // Dprintf("xxx setOversampleChannels: lockAudio\n");
      lockAudio();
      // Dprintf("xxx setOversampleChannels: lockAudio done\n");
      oversample.num_in  = sUI(_numIn);
      oversample.num_out = sUI(_numOut);
      // Dprintf("xxx setOversampleChannels: unlockAudio\n");
      unlockAudio();
      // Dprintf("xxx setOversampleChannels: unlockAudio done\n");
   }

   bool setSampleRate(float _rate, bool _bLock = true) {
      bool r = false;

      // Dprintf("xxx setSampleRate: ENTER bLock=%d\n", _bLock);

      if((_rate >= float(MIN_SAMPLE_RATE)) && (_rate <= float(MAX_SAMPLE_RATE)))
      {
         if(_bLock)
         {
            setGlobals();
            lockAudio();
         }

         sample_rate = _rate;

         vst2_set_samplerate(sample_rate * oversample.factor);  // see engine.cpp

         destroyResamplerStates();

         // Lazy-alloc resampler state
         if(!Dfltequal(oversample.factor, 1.0f))
         {
            int err;

            oversample.srs_in = speex_resampler_init(NUM_INPUTS,
                                                     sUI(sample_rate),  // in rate
                                                     sUI(sample_rate * oversample.factor),  // out rate
                                                     oversample.quality,
                                                     &err
                                                     );

            oversample.srs_out = speex_resampler_init(NUM_OUTPUTS,
                                                      sUI(sample_rate * oversample.factor),  // in rate
                                                      sUI(sample_rate),  // out rate
                                                      oversample.quality,
                                                      &err
                                                      );

            Dprintf("xxx vstrack_plugin: initialize speex resampler (rate=%f factor=%f quality=%d)\n", sample_rate, oversample.factor, oversample.quality);
         }

         if(_bLock)
         {
            unlockAudio();
         }

         r = true;
      }

      // Dprintf("xxx setSampleRate: LEAVE\n");

      return r;
   }

   bool setBlockSize(uint32_t _blockSize) {
      bool r = false;

      if((_blockSize >= MIN_BLOCK_SIZE) && (_blockSize <= MAX_BLOCK_SIZE))
      {
         lockAudio();
         block_size = _blockSize;
         unlockAudio();
         r = true;
      }

      return r;
   }

   void setEnableProcessingActive(bool _bEnable) {
      lockAudio();
      b_processing = _bEnable;
      unlockAudio();
   }

   void checkOffline(void) {
      // Called by VSTPluginProcessReplacingFloat32()
      if(b_check_offline)
      {
         if(NULL != _vstHostCallback)
         {
            int level = (int)_vstHostCallback(&_vstPlugin, audioMasterGetCurrentProcessLevel, 0, 0/*value*/, NULL/*ptr*/, 0.0f/*opt*/);
            // (note) Reason sets process level to kVstProcessLevelUser during "bounce in place"
            bool bOffline = (kVstProcessLevelOffline == level) || (kVstProcessLevelUser == level);

#if 0
            {
               static int i = 0;
               if(0 == (++i & 127))
               {
                  Dprintf("xxx vstrack_plugin: audioMasterGetCurrentProcessLevel: level=%d\n", level);
               }
            }
#endif

            if(b_offline ^ bOffline)
            {
               // Offline mode changed, update resampler
               b_offline = bOffline;

               if(bOffline)
               {
                  Dprintf("xxx vstrack_plugin: enter OFFLINE mode. factor=%f quality=%d\n", oversample.offline_factor, oversample.offline_quality);
                  setOversample(oversample.offline_factor, oversample.offline_quality, false/*bLock*/);
               }
               else
               {
                  Dprintf("xxx vstrack_plugin: enter REALTIME mode. factor=%f quality=%d\n", oversample.realtime_factor, oversample.realtime_quality);
                  setOversample(oversample.realtime_factor, oversample.realtime_quality, false/*bLock*/);
               }

               Dprintf("xxx vstrack_plugin: mode changed to %d\n", int(bOffline));
            }
         }
      }      
   }

   sUI getBankChunk(uint8_t **_addr) {
      return 0;
   }

   void setIdleDetectMode(uint32_t _mode) {
      switch(_mode)
      {
         default:
         case IDLE_DETECT_NONE:
            idle_detect_mode = IDLE_DETECT_NONE;
            break;

         case IDLE_DETECT_MIDI:
            idle_detect_mode = IDLE_DETECT_MIDI;
            break;

         case IDLE_DETECT_AUDIO:
            idle_detect_mode = IDLE_DETECT_AUDIO;
            break;
      }
      b_idle = false;
      idle_output_framecount = 0u;
      idle_frames_since_noteon = 0u;
   }

   uint32_t getCurrentIdleDetectMode(void) {
      uint32_t r = idle_detect_mode;
#ifdef VST2_EFFECT
      if(0u == oversample.num_in)
      {
         idle_detect_mode = IDLE_DETECT_NONE;
      }
#endif
      return r;
   }

   void setIdleDetectModeFx(uint32_t _mode) {
      idle_detect_mode_fx = _mode;
#ifdef VST2_EFFECT
      setIdleDetectMode(uint32_t(_mode));
#endif // VST2_EFFECT
   }

   void setIdleDetectModeInstr(uint32_t _mode) {
      idle_detect_mode_instr = _mode;
#ifndef VST2_EFFECT
      setIdleDetectMode(uint32_t(_mode));
#endif // VST2_EFFECT
   }

   void setIdleGraceSec(float _sec) {
      idle_noteon_sec_grace = _sec;
   }

   float getIdleGraceSec(void) const {
      return idle_noteon_sec_grace;
   }

   void setIdleOutputSec(float _sec) {
      idle_output_sec_threshold = _sec;
   }

   float getIdleOutputSec(void) const {
      return idle_output_sec_threshold;
   }

   void setEnableFixDenorm(int32_t _bEnable) {
      b_fix_denorm = (0 != _bEnable);
      Dprintf("vst2_main:setEnableFixDenorm(%d)\n", b_fix_denorm);
   }

   int32_t getEnableFixDenorm(void) const {
      return int32_t(b_fix_denorm);
   }

   sUI getProgramChunk(uint8_t**_addr) {
      sUI r = 0;
      setGlobals();
      vst2_set_shared_plugin_tls_globals();
      rack::global_ui->app.mtx_param.lock();
      if(NULL != last_program_chunk_str)
      {
         ::free(last_program_chunk_str);
      }
      last_program_chunk_str = rack::global_ui->app.gRackWidget->savePatchToString();
      if(NULL != last_program_chunk_str)
      {
         *_addr = (uint8_t*)last_program_chunk_str;
         r = (sUI)strlen(last_program_chunk_str) + 1/*ASCIIZ*/;
      }
      rack::global_ui->app.mtx_param.unlock();
      return r;
   }

   bool setBankChunk(size_t _size, uint8_t *_addr) {
      bool r = false;
      return r;
   }

   bool setProgramChunk(size_t _size, uint8_t *_addr) {
      Dprintf("xxx vstrack_plugin:setProgramChunk: ENTER\n");
      setGlobals();
      rack::global_ui->app.mtx_param.lock();
      lockAudio();
      vst2_set_shared_plugin_tls_globals();
      rack::global->engine.mutex.lock();
#if 0
      Dprintf("xxx vstrack_plugin:setProgramChunk: size=%u\n", _size);
#endif
      lglw_glcontext_push(rack::global_ui->window.lglw);
      bool r = rack::global_ui->app.gRackWidget->loadPatchFromString((const char*)_addr);
      rack::global_ui->ui.gScene->step();  // w/o this the patch is bypassed
      lglw_glcontext_pop(rack::global_ui->window.lglw);
      rack::global->engine.mutex.unlock();
      unlockAudio();
      rack::global_ui->app.mtx_param.unlock();
      Dprintf("xxx vstrack_plugin:setProgramChunk: LEAVE\n");
      return r;
   }

#ifdef HAVE_WINDOWS
   void sleepMillisecs(uint32_t _num) {
      ::Sleep((DWORD)_num);
   }
#elif defined(HAVE_UNIX)
   void sleepMillisecs(uint32_t _num) {
      ::usleep(1000u * _num);
   }
#endif

   const volatile float *getNextInputChannelChunk(void) {
      volatile float *r = NULL;

      return r;
   }

   volatile float *lockNextOutputChannelChunk(void) {
      volatile float *r = NULL;

      return r;
   }

   sBool getParameterProperties(sUI _index, struct VstParameterProperties *_ret) {
      sBool r = 0;

      ::memset((void*)_ret, 0, sizeof(struct VstParameterProperties));

      if(_index < VST2_MAX_UNIQUE_PARAM_IDS)
      {
         _ret->stepFloat = 0.001f;
         _ret->smallStepFloat = 0.001f;
         _ret->largeStepFloat = 0.01f;
         _ret->flags = 0;
         _ret->displayIndex = VstInt16(_index);
         _ret->category = 0; // 0=no category
         _ret->numParametersInCategory = 0;

         vst2_get_param_name(_index, _ret->label, kVstMaxLabelLen);
         vst2_get_param_name(_index, _ret->shortLabel, kVstMaxShortLabelLen);

         r = 1;
      }

      return r;
   }

   void handleUIParam(int uniqueParamId, float normValue) {
      if(NULL != _vstHostCallback)
         (void)_vstHostCallback(&_vstPlugin, audioMasterAutomate, uniqueParamId, 0/*value*/, NULL/*ptr*/, normValue/*opt*/);
   }

   void getTimingInfo(int *_retPlaying, float *_retBPM, float *_retSongPosPPQ) {
      *_retPlaying = 0;

      if(NULL != _vstHostCallback)
      {
         VstIntPtr result = _vstHostCallback(&_vstPlugin, audioMasterGetTime, 0,
                                             (kVstTransportPlaying | kVstTempoValid | kVstPpqPosValid)/*value=requestmask*/,
                                             NULL/*ptr*/, 0.0f/*opt*/
                                             );
         if(0 != result)
         {
            const struct VstTimeInfo *timeInfo = (const struct VstTimeInfo *)result;

            *_retPlaying = (0 != (timeInfo->flags & kVstTransportPlaying));

            if(0 != (timeInfo->flags & kVstTempoValid))
            {
               *_retBPM = float(timeInfo->tempo);
            }

            if(0 != (timeInfo->flags & kVstPpqPosValid))
            {
               *_retSongPosPPQ = (float)timeInfo->ppqPos;
            }
         }
      }
   }

   void queueRedraw(void) {
      setGlobals();

      if(lglw_window_is_visible(rack::global_ui->window.lglw))
      {
         lglw_redraw(rack::global_ui->window.lglw);
      }
   }

   void redraw(void) {
#if 1
      setGlobals();

      if(lglw_window_is_visible(rack::global_ui->window.lglw))
      {
         vst2_set_shared_plugin_tls_globals();

         // Save DAW GL context and bind our own
         lglw_glcontext_push(rack::global_ui->window.lglw);

#ifdef USE_BEGIN_REDRAW_FXN
         vst2_begin_shared_plugin_redraw();
#endif // USE_BEGIN_REDRAW_FXN

         rack::vst2_editor_redraw();

         // Restore the DAW's GL context
         lglw_glcontext_pop(rack::global_ui->window.lglw);
      }
#endif
   }

#ifdef YAC_LINUX
   void events(void) {
      setGlobals();

      if(lglw_window_is_visible(rack::global_ui->window.lglw))
      {
         lglw_events(rack::global_ui->window.lglw);
      }
   }
#endif // YAC_LINUX

private:
   // the host callback (a function pointer)
   VSTHostCallback _vstHostCallback;

   // the actual structure required by the host
   VSTPlugin _vstPlugin;
};

sSI VSTPluginWrapper::instance_count = 0;



/*******************************************
 * Callbacks: Host -> Plugin
 *
 * Defined here because they are used in the rest of the code later
 */

/**
 * This is the callback that will be called to process the samples in the case of single precision. This is where the
 * meat of the logic happens!
 *
 * @param vstPlugin the object returned by VSTPluginMain
 * @param inputs an array of array of input samples. You read from it. First dimension is for inputs, second dimension is for samples: inputs[numInputs][sampleFrames]
 * @param outputs an array of array of output samples. You write to it. First dimension is for outputs, second dimension is for samples: outputs[numOuputs][sampleFrames]
 * @param sampleFrames the number of samples (second dimension in both arrays)
 */
void VSTPluginProcessReplacingFloat32(VSTPlugin *vstPlugin,
                                      float    **_inputs,
                                      float    **_outputs,
                                      VstInt32   sampleFrames
                                      ) {
   if(sUI(sampleFrames) > VSTPluginWrapper::MAX_BLOCK_SIZE)
      return;  // should not be reachable

   // we can get a hold to our C++ class since we stored it in the `object` field (see constructor)
   VSTPluginWrapper *wrapper = static_cast<VSTPluginWrapper *>(vstPlugin->object);
   // Dprintf("xxx vstrack_plugin: VSTPluginProcessReplacingFloat32: ENTER\n");

   wrapper->setGlobals();
   vst2_set_shared_plugin_tls_globals();

   vst2_handle_queued_params();
   
   wrapper->lockAudio();

   if(wrapper->b_check_offline)
   {
      // Check if offline rendering state changed and update resampler when necessary
      wrapper->checkOffline();
   }

   // // rack::global->engine.vipMutex.lock();
   rack::global->engine.mutex.lock();
   rack::global->vst2.last_seen_num_frames = sUI(sampleFrames);

   //Dprintf("xxx vstrack_plugin: VSTPluginProcessReplacingFloat32: lockAudio done\n");

   //Dprintf("xxx vstrack_plugin: VSTPluginProcessReplacingFloat32: wrapper=%p\n", wrapper);

#ifdef HAVE_WINDOWS
   _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
#endif // HAVE_WINDOWS

#ifdef YAC_LINUX
   fesetround(FE_TOWARDZERO);
#endif // YAC_LINUX

   sUI chIdx;

   if(wrapper->b_idle)
   {
      switch(wrapper->getCurrentIdleDetectMode())
      {
         default:
         case VSTPluginWrapper::IDLE_DETECT_NONE:
            // should not be reachable
            wrapper->b_idle = false;
            wrapper->idle_output_framecount = 0u;
            break;

         case VSTPluginWrapper::IDLE_DETECT_MIDI:
            break;

         case VSTPluginWrapper::IDLE_DETECT_AUDIO:
            {
               for(chIdx = 0u; chIdx < NUM_INPUTS; chIdx++)
               {           
                  if(chIdx < wrapper->oversample.num_in)
                  {
                     cmemptr_t s;
                     s.f32 = _inputs[chIdx];
                     sF32 sum = 0.0f;

                     for(sUI i = 0u; i < sUI(sampleFrames); i++)
                     {
                        mem_t m;
                        m.u32 = s.u32[i] & ~0x80000000u;  // abs
                        sum += m.f32;
                        // sum += (s.f32[i] * s.f32[i]);  // RMS
                     }

                     sum = (sum / float(sampleFrames));
                     // sum = sqrtf(sum / float(sampleFrames));  // RMS

                     if(sum >= wrapper->idle_input_level_threshold)
                     {
                        wrapper->b_idle = false;
                        Dprintf_idle("xxx vstrack_plugin: become active after input (sum=%f, threshold=%f)\n", sum, wrapper->idle_input_level_threshold);
                        wrapper->idle_output_framecount = 0u;
                        break;
                     }
                  }
               }
            }
            break;
      } // switch idle_detect_mode
   } // if idle

   if(!wrapper->b_idle)
   {
      if( !Dfltequal(wrapper->oversample.factor, 1.0f) && 
          (NULL != wrapper->oversample.srs_in)         &&
          (NULL != wrapper->oversample.srs_out)
          )
      {
         sF32 *inputs[NUM_INPUTS];
         sF32 *outputs[NUM_INPUTS];
      
         sUI hostNumFrames = sampleFrames;
         sUI overNumFrames = sUI((sampleFrames * wrapper->oversample.factor) + 0.5f);

         // Up-sample inputs
         {
            sUI inNumFrames = hostNumFrames;
            sUI outNumFrames = overNumFrames;

            sF32 *d = wrapper->oversample.in_buffers;

            for(chIdx = 0u; chIdx < NUM_INPUTS; chIdx++)
            {
               if(chIdx < wrapper->oversample.num_in)
               {
                  sF32 *s = _inputs[chIdx];

                  int err = speex_resampler_process_float(wrapper->oversample.srs_in,
                                                          chIdx,
                                                          s,
                                                          &inNumFrames,
                                                          d,
                                                          &outNumFrames
                                                          );
                  (void)err;
               }
               else
               {
                  // Skip channel
                  ::memset(d, 0, sizeof(sF32) * outNumFrames);
               }

               inputs[chIdx] = d;

               // Next input channel
               d += outNumFrames;
            }
         }

         // Clear output buffers
         //  (note) AudioInterface instances accumulate samples in the output buffer
         {
            sF32 *d = wrapper->oversample.out_buffers;
            ::memset((void*)d, 0, (sizeof(sF32) * wrapper->oversample.num_out * overNumFrames));

            for(chIdx = 0u; chIdx < NUM_OUTPUTS; chIdx++)
            {
               outputs[chIdx] = d;
               d += overNumFrames;
            }
         }

         // Process rack modules
         if(wrapper->b_processing)
         {
            vst2_engine_process(inputs, outputs, overNumFrames);
         }

         // Down-sample outputs
         {
            sF32 *s = wrapper->oversample.out_buffers;

            sUI inNumFrames = overNumFrames;
            sUI outNumFrames = hostNumFrames;

            for(chIdx = 0u; chIdx < NUM_OUTPUTS; chIdx++)
            {
               sF32 *d = _outputs[chIdx];

               if(chIdx < wrapper->oversample.num_out)
               {
                  int err = speex_resampler_process_float(wrapper->oversample.srs_out,
                                                          chIdx,
                                                          s,
                                                          &inNumFrames,
                                                          d,
                                                          &outNumFrames
                                                          );
                  (void)err;

                  // Next output channel
                  s += inNumFrames;
               }
               else
               {
                  // Skip output
                  ::memset((void*)d, 0, sizeof(sF32) * outNumFrames);
               }
            }
         }
      }
      else
      {
         // No oversampling

         //  (note) Cubase (tested with 9.5.30) uses the same buffer(s) for both input&output
         //           => back up the inputs before clearing the outputs
         sF32 *inputs[NUM_INPUTS];
         sUI k = 0u;
         for(chIdx = 0u; chIdx < NUM_INPUTS; chIdx++)
         {
            inputs[chIdx] = &wrapper->tmp_input_buffers[k];
            ::memcpy((void*)inputs[chIdx], _inputs[chIdx], sizeof(sF32)*sampleFrames);
            k += sampleFrames;
         }

         // Clear output buffers
         //  (note) AudioInterface instances accumulate samples in the output buffer
         for(chIdx = 0u; chIdx < NUM_OUTPUTS; chIdx++)
         {
            ::memset((void*)_outputs[chIdx], 0, sizeof(sF32)*sampleFrames);
         }

         if(wrapper->b_processing)
         {
            vst2_engine_process(inputs, _outputs, sampleFrames);
         }
      }

      if(VSTPluginWrapper::IDLE_DETECT_NONE != wrapper->getCurrentIdleDetectMode())
      {
         bool bSilence = true;

         for(chIdx = 0u; chIdx < NUM_OUTPUTS; chIdx++)
         {           
            if(chIdx < wrapper->oversample.num_out)
            {
               cmemptr_t d;
               d.f32 = _outputs[chIdx];
               sF32 sum = 0.0f;

               for(sUI i = 0u; i < sUI(sampleFrames); i++)
               {
                  mem_t m;
                  m.u32 = d.u32[i] & ~0x80000000u;  // abs
                  sum += m.f32;
                  // sum += d.f32[i] * d.f32[i];  // RMS
               }

               sum = (sum / float(sampleFrames));
               // sum = sqrtf(sum / float(sampleFrames));  // RMS

               {
                  static int x = 0;
                  if(0 == (++x & 127))
                  {
                     Dprintf_idle("xxx vstrack_plugin: output avg is %f\n", sum);
                  }
               }

               if(sum >= wrapper->idle_output_level_threshold)
               {
                  bSilence = false;
                  break;
               }
            }
         }

         if(VSTPluginWrapper::IDLE_DETECT_MIDI == wrapper->getCurrentIdleDetectMode())
         {
            wrapper->idle_frames_since_noteon += sampleFrames;
         }

         if(bSilence)
         {
            wrapper->idle_output_framecount += sampleFrames;

            if(VSTPluginWrapper::IDLE_DETECT_MIDI == wrapper->getCurrentIdleDetectMode())
            {
               bSilence = (wrapper->idle_frames_since_noteon >= sUI(wrapper->idle_noteon_sec_grace * wrapper->sample_rate));
            }

            if(wrapper->idle_output_framecount >= sUI(wrapper->idle_output_sec_threshold * wrapper->sample_rate))
            {
               if(bSilence)
               {
                  // Frame threshold exceeded, become idle
                  wrapper->b_idle = true;
                  Dprintf_idle("xxx vstrack_plugin: now idle\n");
               }
            }
         }
         else
         {
            wrapper->idle_output_framecount = 0u;
         }

      } // if idle detect

      // Fix denormalized floats (which can lead to silent audio in FLStudio and Reason)
      if(wrapper->b_fix_denorm)
      {
         for(chIdx = 0u; chIdx < NUM_OUTPUTS; chIdx++)
         {
            float *d = _outputs[chIdx];
            for(int32_t frameIdx = 0; frameIdx < sampleFrames; frameIdx++)
            {
               sF32 t = d[frameIdx];
               t = t + 100.0f;
               t = t - 100.0f;
               if(t >= 4.0f)
                  t = 4.0f;
               else if(t < -4.0f)
                  t = -4.0f;
               d[frameIdx] = t;
            }
         }
      }

   } // if !wrapper->b_idle
   else
   {
      // Idle, clear output buffers
      for(chIdx = 0u; chIdx < NUM_OUTPUTS; chIdx++)
      {
         ::memset((void*)_outputs[chIdx], 0, sizeof(sF32)*sampleFrames);
      }
   }

   // // rack::global->engine.vipMutex.unlock();
   rack::global->engine.mutex.unlock();
   wrapper->unlockAudio();

   //Dprintf("xxx vstrack_plugin: VSTPluginProcessReplacingFloat32: LEAVE\n");
}



#ifdef VST2_OPCODE_DEBUG
static const char *vst2_opcode_names[] = {
	"effOpen",
	"effClose",
	"effSetProgram",
	"effGetProgram",
	"effSetProgramName",
	"effGetProgramName",
   "effGetParamLabel",
	"effGetParamDisplay",
	"effGetParamName",
	"DEPR_effGetVu",
	"effSetSampleRate",
	"effSetBlockSize",
	"effMainsChanged",
	"effEditGetRect",
	"effEditOpen",
	"effEditClose",
	"DEPR_effEditDraw",
	"DEPR_effEditMouse",
	"DEPR_effEditKey",
	"effEditIdle",
   "DEPR_effEditTop",
	"DEPR_effEditSleep",
	"DEPR_effIdentify",
   "effGetChunk",
	"effSetChunk",
   // extended:
	"effProcessEvents",
	"effCanBeAutomated",
	"effString2Parameter",
	"effGetNumProgramCategories",
	"effGetProgramNameIndexed",
   "DEPR_effCopyProgram",
	"DEPR_effConnectInput",
	"DEPR_effConnectOutput",
   "effGetInputProperties",
	"effGetOutputProperties",
	"effGetPlugCategory",
	"DEPR_effGetCurrentPosition",
	"DEPR_effGetDestinationBuffer",
	"effOfflineNotify",
	"effOfflinePrepare",
	"effOfflineRun",
	"effProcessVarIo",
	"effSetSpeakerArrangement",
	"DEPR_effSetBlockSizeAndSampleRate",
	"effSetBypass",
	"effGetEffectName",
	"DEPR_effGetErrorText",
	"effGetVendorString",
	"effGetProductString",
	"effGetVendorVersion",
	"effVendorSpecific",
	"effCanDo",
	"effGetTailSize",
	"DEPR_effIdle",
	"DEPR_effGetIcon",
	"DEPR_effSetViewPosition",
	"effGetParameterProperties",
	"DEPR_effKeysRequired",
	"effGetVstVersion",
	"effEditKeyDown",
	"effEditKeyUp",
	"effSetEditKnobMode",
	"effGetMidiProgramName",
	"effGetCurrentMidiProgram",
	"effGetMidiProgramCategory",
	"effHasMidiProgramsChanged",
	"effGetMidiKeyName",
   "effBeginSetProgram",
	"effEndSetProgram",
	"effGetSpeakerArrangement",
	"effShellGetNextPlugin",
	"effStartProcess",
	"effStopProcess",
	"effSetTotalSampleToProcess",
	"effSetPanLaw",
   "effBeginLoadBank",
	"effBeginLoadProgram",
	"effSetProcessPrecision",
	"effGetNumMidiInputChannels",
	"effGetNumMidiOutputChannels",
};
#endif // VST2_OPCODE_DEBUG

/**
 * This is the plugin called by the host to communicate with the plugin, mainly to request information (like the
 * vendor string, the plugin category...) or communicate state/changes (like open/close, frame rate...)
 *
 * @param vstPlugin the object returned by VSTPluginMain
 * @param opCode defined in aeffect.h/AEffectOpcodes and which continues in aeffectx.h/AEffectXOpcodes for a grand
 *        total of 79 of them! Only a few of them are implemented in this small plugin.
 * @param index depend on the opcode
 * @param value depend on the opcode
 * @param ptr depend on the opcode
 * @param opt depend on the opcode
 * @return depend on the opcode (0 is ok when you don't implement an opcode...)
 */
VstIntPtr VSTPluginDispatcher(VSTPlugin *vstPlugin,
                              VstInt32   opCode,
                              VstInt32   index,
                              VstIntPtr  value,
                              void      *ptr,
                              float      opt
                              ) {
#ifdef VST2_OPCODE_DEBUG
   Dprintf("vstrack_plugin: called VSTPluginDispatcher(%d) (%s)\n", opCode, vst2_opcode_names[opCode]);
#else
   // Dprintf("vstrack_plugin: called VSTPluginDispatcher(%d)\n", opCode);
#endif // VST2_OPCODE_DEBUG

   VstIntPtr r = 0;

   // we can get a hold to our C++ class since we stored it in the `object` field (see constructor)
   VSTPluginWrapper *wrapper = static_cast<VSTPluginWrapper *>(vstPlugin->object);

   // see aeffect.h/AEffectOpcodes and aeffectx.h/AEffectXOpcodes for details on all of them
   switch(opCode)
   {
      case effGetPlugCategory:
         // request for the category of the plugin: in this case it is an effect since it is modifying the input (as opposed
         // to generating sound)
#ifdef VST2_EFFECT
         return kPlugCategEffect;
#else
         return kPlugCategSynth;
#endif // VST2_EFFECT

      case effOpen:
         // called by the host after it has obtained the effect instance (but _not_ during plugin scans)
         //  (note) any heavy-lifting init code should go here
         Dprintf("vstrack_plugin<dispatcher>: effOpen\n");
         r = wrapper->openEffect();
         break;

      case effClose:
         // called by the host when the plugin was called... time to reclaim memory!
         wrapper->closeEffect();
         // (note) hosts usually call effStopProcess before effClose
         delete wrapper;
         break;

      case effSetProgram:
         r = 1;
         break;

      case effGetProgram:
         r = 0;
         break;

      case effGetVendorString:
         // request for the vendor string (usually used in the UI for plugin grouping)
         ::strncpy(static_cast<char *>(ptr), "bsp", kVstMaxVendorStrLen);
         r = 1;
         break;

      case effGetVendorVersion:
         // request for the version
         return PLUGIN_VERSION;

      case effVendorSpecific:
         break;

      case effGetVstVersion:
          r = kVstVersion;
          break;

      case effGetEffectName:
#ifdef VST2_EFFECT
         ::strncpy((char*)ptr, "VeeSeeVST Rack 0.6.1", kVstMaxEffectNameLen);
#else
         ::strncpy((char*)ptr, "VeeSeeVST Rack 0.6.1 I", kVstMaxEffectNameLen);
#endif // VST2_EFFECT
         r = 1;
         break;

      case effGetProductString:
#ifdef VST2_EFFECT
         ::strncpy((char*)ptr, "VeeSeeVST Rack 0.6.1 VST2 Plugin v0.4", kVstMaxProductStrLen);
#else
         ::strncpy((char*)ptr, "VeeSeeVST Rack 0.6.1 I VST2 Plugin v0.4", kVstMaxProductStrLen);
#endif // VST2_EFFECT
         r = 1;
         break;

      case effGetNumMidiInputChannels:
         r = 16;
         break;

      case effGetNumMidiOutputChannels:
         r = 0;
         break;

      case effGetInputProperties:
         {
            VstPinProperties *pin = (VstPinProperties*)ptr;
            ::snprintf(pin->label, kVstMaxLabelLen, "Input #%d", index);
            pin->flags           = kVstPinIsActive | ((0 == (index & 1)) ? kVstPinIsStereo : 0);
            pin->arrangementType = ((0 == (index & 1)) ? kSpeakerArrStereo : kSpeakerArrMono);
            ::snprintf(pin->shortLabel, kVstMaxShortLabelLen, "in%d", index);
            memset((void*)pin->future, 0, 48);
            r = 1;
         }
         break;

      case effGetOutputProperties:
         {
            VstPinProperties *pin = (VstPinProperties*)ptr;
            ::snprintf(pin->label, kVstMaxLabelLen, "Output #%d", index);
            pin->flags           = kVstPinIsActive | ((0 == (index & 1)) ? kVstPinIsStereo : 0);
            pin->arrangementType = ((0 == (index & 1)) ? kSpeakerArrStereo : kSpeakerArrMono);
            ::snprintf(pin->shortLabel, kVstMaxShortLabelLen, "out%d", index);
            memset((void*)pin->future, 0, 48);
            r = 1;
         }
         break;

      case effSetSampleRate:
         r = wrapper->setSampleRate(opt) ? 1 : 0;
         break;

      case effSetBlockSize:
         r = wrapper->setBlockSize(uint32_t(value)) ? 1 : 0;
         break;

      case effCanDo:
         // ptr:
         // "sendVstEvents"
         // "sendVstMidiEvent"
         // "sendVstTimeInfo"
         // "receiveVstEvents"
         // "receiveVstMidiEvent"
         // "receiveVstTimeInfo"
         // "offline"
         // "plugAsChannelInsert"
         // "plugAsSend"
         // "mixDryWet"
         // "noRealTime"
         // "multipass"
         // "metapass"
         // "1in1out"
         // "1in2out"
         // "2in1out"
         // "2in2out"
         // "2in4out"
         // "4in2out"
         // "4in4out"
         // "4in8out"
         // "8in4out"
         // "8in8out"
         // "midiProgramNames"
         // "conformsToWindowRules"
#ifdef VST2_OPCODE_DEBUG
         printf("xxx effCanDo \"%s\" ?\n", (char*)ptr);
#endif // VST2_OPCODE_DEBUG
         if(!strcmp((char*)ptr, "receiveVstEvents"))
            r = 1;
         else if(!strcmp((char*)ptr, "receiveVstMidiEvent"))  // (note) required by Jeskola Buzz
            r = 1;
         else
            r = 0;
         break;

      case effGetProgramName:
         ::snprintf((char*)ptr, kVstMaxProgNameLen, "default");
         r = 1;
         break;

      case effSetProgramName:
         r = 1;
         break;

      case effGetProgramNameIndexed:
         ::sprintf((char*)ptr, "default");
         r = 1;
         break;

      case effGetParamName:
         // kVstMaxParamStrLen(8), much longer in other plugins
         // printf("xxx vstrack_plugin: effGetParamName: ptr=%p\n", ptr);
         wrapper->setGlobals();
         vst2_get_param_name(index, (char*)ptr, kVstMaxParamStrLen);
         r = 1;
         break;

      case effCanBeAutomated:
         // fix Propellerhead Reason VST parameter support
         r = 1;
         break;

      case effGetParamLabel:
         // e.g. "dB"
         break;

      case effGetParamDisplay:
         // e.g. "-20"
         break;

#if 0
      case effGetParameterProperties:
         // [index]: parameter index [ptr]: #VstParameterProperties* [return value]: 1 if supported
         wrapper->setGlobals();
         r = wrapper->getParameterProperties(sUI(index), (struct VstParameterProperties*)ptr);
         break;
#endif

      case effGetChunk:
         // Query bank (index=0) or program (index=1) state
         //  value: 0
         //    ptr: buffer address
         //      r: buffer size
         Dprintf("xxx vstrack_plugin: effGetChunk index=%d ptr=%p\n", index, ptr);
         // // if(0 == index)
         // // {
         // //    r = wrapper->getBankChunk((uint8_t**)ptr);
         // // }
         // // else
         // // {
            r = wrapper->getProgramChunk((uint8_t**)ptr);
         // // }
         break;

      case effSetChunk:
         // Restore bank (index=0) or program (index=1) state
         //  value: buffer size
         //    ptr: buffer address
         //      r: 1
         Dprintf("xxx vstrack_plugin: effSetChunk index=%d size=%d ptr=%p\n", index, (int)value, ptr);
         // // if(0 == index)
         // // {
         // //    r = wrapper->setBankChunk(size_t(value), (uint8_t*)ptr) ? 1 : 0;
         // // }
         // // else
         // // {
            r = wrapper->setProgramChunk(size_t(value), (uint8_t*)ptr) ? 1 : 0;
         // // }
         Dprintf("xxx vstrack_plugin: effSetChunk LEAVE\n");
         break;

      case effShellGetNextPlugin:
         // For shell plugins (e.g. Waves), returns next sub-plugin UID (or 0)
         //  (note) plugin uses audioMasterCurrentId while it's being instantiated to query the currently selected sub-plugin
         //          if the host returns 0, it will then call effShellGetNextPlugin to enumerate the sub-plugins
         //  ptr: effect name string ptr (filled out by the plugin)
         r = 0;
         break;

      case effMainsChanged:
         // value = 0=suspend, 1=resume
         wrapper->setEnableProcessingActive((value > 0) ? true : false);
         r = 1;
         break;

      case effStartProcess:
         wrapper->setEnableProcessingActive(true);
         r = 1;
         break;

      case effStopProcess:
         wrapper->setEnableProcessingActive(false);
         r = 1;
         break;

      case effProcessEvents:
         // ptr: VstEvents*
         {
            VstEvents *events = (VstEvents*)ptr;
            // Dprintf("vstrack_plugin:effProcessEvents: recvd %d events", events->numEvents);
            VstEvent**evAddr = &events->events[0];

            if(events->numEvents > 0)
            {
               wrapper->setGlobals();
               wrapper->mtx_mididev.lock();
            
               for(uint32_t evIdx = 0u; evIdx < uint32_t(events->numEvents); evIdx++, evAddr++)
               {
                  VstEvent *ev = *evAddr;

                  if(NULL != ev)  // paranoia
                  {
#ifdef DEBUG_PRINT_EVENTS
                     Dprintf("vstrack_plugin:effProcessEvents: ev[%u].byteSize    = %u\n", evIdx, uint32_t(ev->byteSize));  // sizeof(VstMidiEvent) = 32
                     Dprintf("vstrack_plugin:effProcessEvents: ev[%u].deltaFrames = %u\n", evIdx, uint32_t(ev->deltaFrames));
#endif // DEBUG_PRINT_EVENTS

                     switch(ev->type)
                     {
                        default:
                           //case kVstAudioType:      // deprecated
                           //case kVstVideoType:      // deprecated
                           //case kVstParameterType:  // deprecated
                           //case kVstTriggerType:    // deprecated
                           break;

                        case kVstMidiType:
                           // (note) ev->data stores the actual payload (up to 16 bytes)
                           // (note) e.g. 0x90 0x30 0x7F for a C-4 note-on on channel 1 with velocity 127
                           // (note) don't forget to use a mutex (lockAudio(), unlockAudio()) when modifying the audio processor state!
                        {
                           VstMidiEvent *mev = (VstMidiEvent *)ev;
#ifdef DEBUG_PRINT_EVENTS
                           Dprintf("vstrack_plugin:effProcessEvents<midi>: ev[%u].noteLength      = %u\n", evIdx, uint32_t(mev->noteLength));  // #frames
                           Dprintf("vstrack_plugin:effProcessEvents<midi>: ev[%u].noteOffset      = %u\n", evIdx, uint32_t(mev->noteOffset));  // #frames
                           Dprintf("vstrack_plugin:effProcessEvents<midi>: ev[%u].midiData        = %02x %02x %02x %02x\n", evIdx, uint8_t(mev->midiData[0]), uint8_t(mev->midiData[1]), uint8_t(mev->midiData[2]), uint8_t(mev->midiData[3]));
                           Dprintf("vstrack_plugin:effProcessEvents<midi>: ev[%u].detune          = %d\n", evIdx, mev->detune); // -64..63
                           Dprintf("vstrack_plugin:effProcessEvents<midi>: ev[%u].noteOffVelocity = %d\n", evIdx, mev->noteOffVelocity); // 0..127
#endif // DEBUG_PRINT_EVENTS

                           if(VSTPluginWrapper::IDLE_DETECT_MIDI == wrapper->getCurrentIdleDetectMode())
                           {
                              if(0x90u == (mev->midiData[0] & 0xF0u)) // Note on ?
                              {
                                 wrapper->lockAudio();
                                 if(wrapper->b_idle)
                                 {
                                    Dprintf_idle("xxx vstrack_plugin: become active after MIDI note on\n");
                                 }
                                 wrapper->b_idle = false;
                                 wrapper->idle_output_framecount = 0u;
                                 wrapper->idle_frames_since_noteon = 0u;
                                 wrapper->unlockAudio();
                              }
                           }

                           vst2_process_midi_input_event(mev->midiData[0],
                                                         mev->midiData[1],
                                                         mev->midiData[2]
                                                         );

                        }
                        break;

                        case kVstSysExType:
                        {
#ifdef DEBUG_PRINT_EVENTS
                           VstMidiSysexEvent *xev = (VstMidiSysexEvent*)ev;
                           Dprintf("vstrack_plugin:effProcessEvents<syx>: ev[%u].dumpBytes = %u\n", evIdx, uint32_t(xev->dumpBytes));  // size
                           Dprintf("vstrack_plugin:effProcessEvents<syx>: ev[%u].sysexDump = %p\n", evIdx, xev->sysexDump);            // buffer addr
#endif // DEBUG_PRINT_EVENTS

                           // (note) don't forget to use a mutex (lockAudio(), unlockAudio()) when modifying the audio processor state!
                        }
                        break;
                     }
                  } // if ev
               } // loop events

               wrapper->mtx_mididev.unlock();
            } // if events
         }
         break;

      case effGetTailSize: // 52
         break;
#if 1
      //case effIdle:
      case 53:
         // Periodic idle call (from UI thread), e.g. at 20ms intervals (depending on host)
         //  (note) deprecated in vst2.4 (but some plugins still rely on this)
         r = 1;
         break;
#endif

      case effEditIdle:
#ifdef YAC_LINUX
         // pump event queue (when not using _XEventProc callback)
         wrapper->events();
#endif // YAC_LINUX
         if(0 == wrapper->redraw_ival_ms)
         {
            wrapper->queueRedraw();
         }
         break;

      case effEditGetRect:
         // Query editor window geometry
         // ptr: ERect* (on Windows)
         if(NULL != ptr)
         {
            // ...
            printf("xxx vstrack_plugin: effEditGetRect: (%d; %d; %d; %d)\n",
                   wrapper->editor_rect.top,
                   wrapper->editor_rect.left,
                   wrapper->editor_rect.bottom,
                   wrapper->editor_rect.right
                   );
            *(void**)ptr = (void*) &wrapper->editor_rect;
            r = 1;
         }
         else
         {
            r = 0;
         }
         break;

#if 0
      case effEditTop:
         // deprecated in vst2.4
         r = 0;
         break;
#endif

      case effEditOpen:
         // Show editor window
         // ptr: native window handle (hWnd on Windows)
         wrapper->openEditor(ptr);
         r = 1;
         break;

      case effEditClose:
         // Close editor window
         wrapper->closeEditor();
         r = 1;
         break;

      case effEditKeyDown:
         // [index]: ASCII character [value]: virtual key [opt]: modifiers [return value]: 1 if key used  @see AEffEditor::onKeyDown
         // (note) only used for touch input
         // Dprintf("xxx effEditKeyDown: ascii=%d (\'%c\') vkey=0x%08x mod=0x%08x\n", index, index, value, opt);
         if(rack::b_touchkeyboard_enable)
         {
            wrapper->setGlobals();
            {
               uint32_t vkey = 0u;

               switch(uint32_t(value))
               {
                  // see aeffectx.h
                  case VKEY_BACK:      vkey = LGLW_VKEY_BACKSPACE; break;
                  case VKEY_TAB:       vkey = LGLW_VKEY_TAB;       break;
                  case VKEY_RETURN:    vkey = LGLW_VKEY_RETURN;    break;
                  case VKEY_ESCAPE:    vkey = LGLW_VKEY_ESCAPE;    break;
                  case VKEY_END:       vkey = LGLW_VKEY_END;       break;
                  case VKEY_HOME:      vkey = LGLW_VKEY_HOME;      break;
                  case VKEY_LEFT:      vkey = LGLW_VKEY_LEFT;      break;
                  case VKEY_UP:        vkey = LGLW_VKEY_UP;        break;
                  case VKEY_RIGHT:     vkey = LGLW_VKEY_RIGHT;     break;
                  case VKEY_DOWN:      vkey = LGLW_VKEY_DOWN;      break;
                  case VKEY_PAGEUP:    vkey = LGLW_VKEY_PAGEUP;    break;
                  case VKEY_PAGEDOWN:  vkey = LGLW_VKEY_PAGEDOWN;  break;
                  case VKEY_ENTER:     vkey = LGLW_VKEY_RETURN;    break;
                  case VKEY_INSERT:    vkey = LGLW_VKEY_INSERT;    break;
                  case VKEY_DELETE:    vkey = LGLW_VKEY_DELETE;    break;
                     break;

                  default:
                     vkey = (char)index;
                     // (note) some(most?) DAWs don't send the correct VKEY_xxx values for all special keys
                     switch(vkey)
                     {
                        case 8:  vkey = LGLW_VKEY_BACKSPACE;  break;
                           //    case 13: vkey = LGLW_VKEY_RETURN;     break;
                           //    case 9:  vkey = LGLW_VKEY_TAB;        break;
                           //    case 27: vkey = LGLW_VKEY_ESCAPE;     break;
                        default:
                           if(vkey >= 128)
                              vkey = 0u;
                           break;
                     }
                     break;
               }
            
               if(vkey > 0u)
               {
                  r = vst2_handle_effeditkeydown(vkey);
               }
            }
         }
         break;

      case effGetParameterProperties/*56*/:
      case effGetMidiKeyName/*66*/:
         // (todo) Bitwig (Linux) sends a lot of them
         break;

      default:
         // ignoring all other opcodes
         Dprintf("vstrack_plugin:dispatcher: unhandled opCode %d [ignored] \n", opCode);
         break;

   }

   return r;
}


/**
 * Set parameter setting
 */
void VSTPluginSetParameter(VSTPlugin *vstPlugin,
                           VstInt32   index,
                           float      parameter
                           ) {
#ifdef DEBUG_PRINT_PARAMS
   Dprintf("vstrack_plugin: called VSTPluginSetParameter(%d, %f)\n", index, parameter);
#endif // DEBUG_PRINT_PARAMS

   // we can get a hold to our C++ class since we stored it in the `object` field (see constructor)
   VSTPluginWrapper *wrapper = static_cast<VSTPluginWrapper *>(vstPlugin->object);

   // // wrapper->lockAudio();
   wrapper->setGlobals();
   rack::global_ui->app.mtx_param.lock();
   vst2_queue_param(index, parameter, true/*bNormalized*/);
   rack::global_ui->app.mtx_param.unlock();
   // // wrapper->unlockAudio();
}

void vst2_queue_param_sync(int _uniqueParamId, float _value, bool _bNormalized) {
   // Called when parameter is edited numerically via textfield
   printf("xxx vst2_queue_param_sync ENTER: uniqueParamId=%d value=%f bNormalized=%d\n", _uniqueParamId, _value, _bNormalized);
   // // VSTPluginWrapper *wrapper = rack::global->vst2.wrapper;

   // // wrapper->lockAudio();
   rack::global_ui->app.mtx_param.lock();
   vst2_queue_param(_uniqueParamId, _value, _bNormalized);
   rack::global_ui->app.mtx_param.unlock();
   // // wrapper->unlockAudio();
   printf("xxx vst2_queue_param_sync LEAVE\n");
}


/**
 * Query parameter
 */
float VSTPluginGetParameter(VSTPlugin *vstPlugin,
                            VstInt32   index
                            ) {
#ifdef DEBUG_PRINT_PARAMS
   Dprintf("vstrack_plugin: called VSTPluginGetParameter(%d)\n", index);
#endif // DEBUG_PRINT_PARAMS
   // we can get a hold to our C++ class since we stored it in the `object` field (see constructor)
   VSTPluginWrapper *wrapper = static_cast<VSTPluginWrapper *>(vstPlugin->object);

   wrapper->setGlobals();
   wrapper->lockAudio();
   rack::global->engine.mutex.lock();   // don't query a param while a module is being deleted

   float r = vst2_get_param(index);

   rack::global->engine.mutex.unlock();
   wrapper->unlockAudio();

   return r;
}


/**
 * Main constructor for our C++ class
 */
VSTPluginWrapper::VSTPluginWrapper(audioMasterCallback vstHostCallback,
                                   VstInt32 vendorUniqueID,
                                   VstInt32 vendorVersion,
                                   VstInt32 numParams,
                                   VstInt32 numPrograms,
                                   VstInt32 numInputs,
                                   VstInt32 numOutputs
                                   ) : _vstHostCallback(vstHostCallback)
{
   instance_count++;

   // Make sure that the memory is properly initialized
   memset(&_vstPlugin, 0, sizeof(_vstPlugin));

   // this field must be set with this constant...
   _vstPlugin.magic = kEffectMagic;

   // storing this object into the VSTPlugin so that it can be retrieved when called back (see callbacks for use)
   _vstPlugin.object = this;

   // specifying that we handle both single and NOT double precision (there are other flags see aeffect.h/VstAEffectFlags)
   _vstPlugin.flags = 
#ifndef VST2_EFFECT
      effFlagsIsSynth                  |
#endif
      effFlagsCanReplacing             | 
      // (effFlagsCanDoubleReplacing & 0) |
      effFlagsProgramChunks            |
      effFlagsHasEditor                ;

   // initializing the plugin with the various values
   _vstPlugin.uniqueID    = vendorUniqueID;
   _vstPlugin.version     = vendorVersion;
   _vstPlugin.numParams   = numParams;
   _vstPlugin.numPrograms = numPrograms;
   _vstPlugin.numInputs   = numInputs;
   _vstPlugin.numOutputs  = numOutputs;

   // setting the callbacks to the previously defined functions
   _vstPlugin.dispatcher             = &VSTPluginDispatcher;
   _vstPlugin.getParameter           = &VSTPluginGetParameter;
   _vstPlugin.setParameter           = &VSTPluginSetParameter;
   _vstPlugin.processReplacing       = &VSTPluginProcessReplacingFloat32;
   _vstPlugin.processDoubleReplacing = NULL;//&VSTPluginProcessReplacingFloat64;

   // report latency
   _vstPlugin.initialDelay = 0;

   oversample.factor           = 1.0f;
   oversample.quality          = SPEEX_RESAMPLER_QUALITY_DEFAULT;
   oversample.realtime_factor  = 1.0f;
   oversample.realtime_quality = SPEEX_RESAMPLER_QUALITY_DEFAULT;
   oversample.offline_factor   = 1.0f;
   oversample.offline_quality  = SPEEX_RESAMPLER_QUALITY_DEFAULT;
   oversample.srs_in  = NULL;
   oversample.srs_out = NULL;
   oversample.num_in  = NUM_INPUTS;
   oversample.num_out = NUM_OUTPUTS;

   sample_rate  = 44100.0f;
   block_size   = 64u;
   b_processing = true;
   b_offline    = false;
   b_check_offline = false;

   idle_detect_mode            = IDLE_DETECT_NONE;
   idle_detect_mode_fx         = IDLE_DETECT_AUDIO;
   idle_detect_mode_instr      = IDLE_DETECT_MIDI;
   b_idle                      = false;
   idle_input_level_threshold  = 0.00018f;//0.00007f;
   idle_output_level_threshold = 0.00018f;//0.00003f;
   idle_output_sec_threshold   = 0.2f;  // idle after 200ms of silence
   idle_output_framecount      = 0u;
   idle_noteon_sec_grace       = 0.3f;  // grace period after note on
   idle_frames_since_noteon    = 0u;

   b_fix_denorm = false;

   last_program_chunk_str = NULL;

   b_open = false;
   b_editor_open = false;

   editor_rect.left   = EDITWIN_X;
   editor_rect.top    = EDITWIN_Y;
   editor_rect.right  = EDITWIN_X + EDITWIN_W;
   editor_rect.bottom = EDITWIN_Y + EDITWIN_H;

   redraw_ival_ms = 0;
}

/**
 * Destructor called when the plugin is closed (see VSTPluginDispatcher with effClose opCode). In this very simply plugin
 * there is nothing to do but in general the memory that gets allocated MUST be freed here otherwise there might be a
 * memory leak which may end up slowing down and/or crashing the host
 */
VSTPluginWrapper::~VSTPluginWrapper() {
   closeEffect();
   instance_count--;
}


void vst2_lock_midi_device() {
   rack::global->vst2.wrapper->mtx_mididev.lock();
}

void vst2_unlock_midi_device() {
   rack::global->vst2.wrapper->mtx_mididev.unlock();
}

void vst2_handle_ui_param(int uniqueParamId, float normValue) {
   // Called by engineSetParam()
   // printf("xxx vst2_handle_ui_param: uniqueParamId=%d &global=%p global=%p wrapper=%p\n", uniqueParamId, &rack::global, rack::global, rack::global->vst2.wrapper);
   rack::global->vst2.wrapper->handleUIParam(uniqueParamId, normValue);
}

void vst2_get_timing_info(int *_retPlaying, float *_retBPM, float *_retSongPosPPQ) {
   // updates the requested fields when query was successful
   rack::global->vst2.wrapper->getTimingInfo(_retPlaying, _retBPM, _retSongPosPPQ);
}

void vst2_set_globals(void *_wrapper) {
   VSTPluginWrapper *wrapper = (VSTPluginWrapper *)_wrapper;
   wrapper->setGlobals();
   vst2_set_shared_plugin_tls_globals();
}

void vst2_window_size_set(int _width, int _height) {
   rack::global->vst2.wrapper->setWindowSize(_width, _height);
}

void vst2_refresh_rate_set(float _hz) {
   rack::global->vst2.wrapper->setRefreshRate(_hz);
}

float vst2_refresh_rate_get(void) {
   return rack::global->vst2.wrapper->getRefreshRate();
}

extern "C" {
void lglw_timer_cbk(lglw_t _lglw) {
   VSTPluginWrapper *wrapper = (VSTPluginWrapper*)lglw_userdata_get(_lglw);
   wrapper->queueRedraw();
}
}

extern "C" {
void lglw_redraw_cbk(lglw_t _lglw) {
   VSTPluginWrapper *wrapper = (VSTPluginWrapper*)lglw_userdata_get(_lglw);
   wrapper->redraw();
}
}

void vst2_oversample_realtime_set(float _factor, int _quality) {
   rack::global->vst2.wrapper->setOversampleRealtime(_factor, _quality);
}

void vst2_oversample_realtime_get(float *_factor, int *_quality) {
   *_factor  = rack::global->vst2.wrapper->oversample.realtime_factor;
   *_quality = int(rack::global->vst2.wrapper->oversample.realtime_quality);
}

void vst2_oversample_offline_set(float _factor, int _quality) {
   rack::global->vst2.wrapper->setOversampleOffline(_factor, _quality);
}

void vst2_oversample_offline_get(float *_factor, int *_quality) {
   *_factor  = rack::global->vst2.wrapper->oversample.offline_factor;
   *_quality = int(rack::global->vst2.wrapper->oversample.offline_quality);
}

void vst2_oversample_offline_check_set(int _bEnable) {
   rack::global->vst2.wrapper->b_check_offline = (0 != _bEnable);
}

int32_t vst2_oversample_offline_check_get(void) {
   return int32_t(rack::global->vst2.wrapper->b_check_offline);
}

void vst2_oversample_channels_set(int _numIn, int _numOut) {
   rack::global->vst2.wrapper->setOversampleChannels(_numIn, _numOut);
}

void vst2_oversample_channels_get(int *_numIn, int *_numOut) {
   *_numIn  = int(rack::global->vst2.wrapper->oversample.num_in);
   *_numOut = int(rack::global->vst2.wrapper->oversample.num_out);
}

void vst2_idle_detect_mode_fx_set(int _mode) {
   rack::global->vst2.wrapper->setIdleDetectModeFx(uint32_t(_mode));
}

int vst2_idle_detect_mode_fx_get(void) {
   return rack::global->vst2.wrapper->idle_detect_mode_fx;
}

void vst2_idle_detect_mode_instr_set(int _mode) {
   rack::global->vst2.wrapper->setIdleDetectModeInstr(uint32_t(_mode));
}

int vst2_idle_detect_mode_instr_get(void) {
   return rack::global->vst2.wrapper->idle_detect_mode_instr;
}

void vst2_idle_detect_mode_set(int _mode) {
   rack::global->vst2.wrapper->setIdleDetectMode(uint32_t(_mode));
}

void vst2_idle_detect_mode_get(int *_mode) {
   *_mode = int(rack::global->vst2.wrapper->idle_detect_mode);
}

void vst2_idle_grace_sec_set(float _sec) {
   // Note-ons / MIDI idle detect mode
   if(_sec < 0.05f)
      _sec = 0.05f;
   else if(_sec > 3.0f)
      _sec = 3.0f;
   rack::global->vst2.wrapper->setIdleGraceSec(_sec);
}

float vst2_idle_grace_sec_get(void) {
   return rack::global->vst2.wrapper->getIdleGraceSec();
}

void vst2_idle_output_sec_set(float _sec) {
   if(_sec < 0.05f)
      _sec = 0.05f;
   else if(_sec > 3.0f)
      _sec = 3.0f;
   rack::global->vst2.wrapper->setIdleOutputSec(_sec);
}

float vst2_idle_output_sec_get(void) {
   return rack::global->vst2.wrapper->getIdleOutputSec();
}

int vst2_fix_denorm_get(void) {
   return rack::global->vst2.wrapper->getEnableFixDenorm();
}

void vst2_fix_denorm_set(int _bEnable) {
   rack::global->vst2.wrapper->setEnableFixDenorm(_bEnable);
}


/**
 * Implementation of the main entry point of the plugin
 */
VST_EXPORT VSTPlugin *VSTPluginMain(VSTHostCallback vstHostCallback) {

#ifdef USE_LOG_PRINTF
   logfile = fopen("/tmp/vst2_log.txt", "w");
#endif // USE_LOG_PRINTF

   Dprintf("vstrack_plugin: called VSTPluginMain... \n");

#if 0
   if(!vstHostCallback(0, audioMasterVersion, 0, 0, 0, 0))
   {
		return 0;  // old version
   }
#endif

   // simply create our plugin C++ class
   VSTPluginWrapper *plugin =
      new VSTPluginWrapper(vstHostCallback,
                           // registered with Steinberg (http://service.steinberg.de/databases/plugin.nsf/plugIn?openForm)
#ifdef VST2_EFFECT
                           CCONST('g', 'v', 'g', 'y'),
#else
                           CCONST('v', '5', 'k', 'v'),
#endif // VST2_EFFECT
                           PLUGIN_VERSION, // version
                           VST2_MAX_UNIQUE_PARAM_IDS,    // num params
                           1,    // one program
                           NUM_INPUTS,
                           NUM_OUTPUTS
                           );

   // return the plugin per the contract of the API
   return plugin->getVSTPlugin();
}
#endif // USE_VST2
