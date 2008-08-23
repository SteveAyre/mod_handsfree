/* ----------------------------------------------------------------------------
 * This file was automatically generated by SWIG (http://www.swig.org).
 * Version 1.3.36
 *
 * Do not make changes to this file unless you know what you are doing--modify
 * the SWIG interface file instead.
 * ----------------------------------------------------------------------------- */

namespace FreeSWITCH.Native {

using System;
using System.Runtime.InteropServices;

public class switch_codec_settings : IDisposable {
  private HandleRef swigCPtr;
  protected bool swigCMemOwn;

  internal switch_codec_settings(IntPtr cPtr, bool cMemoryOwn) {
    swigCMemOwn = cMemoryOwn;
    swigCPtr = new HandleRef(this, cPtr);
  }

  internal static HandleRef getCPtr(switch_codec_settings obj) {
    return (obj == null) ? new HandleRef(null, IntPtr.Zero) : obj.swigCPtr;
  }

  ~switch_codec_settings() {
    Dispose();
  }

  public virtual void Dispose() {
    lock(this) {
      if(swigCPtr.Handle != IntPtr.Zero && swigCMemOwn) {
        swigCMemOwn = false;
        freeswitchPINVOKE.delete_switch_codec_settings(swigCPtr);
      }
      swigCPtr = new HandleRef(null, IntPtr.Zero);
      GC.SuppressFinalize(this);
    }
  }

  public int quality {
    set {
      freeswitchPINVOKE.switch_codec_settings_quality_set(swigCPtr, value);
    } 
    get {
      int ret = freeswitchPINVOKE.switch_codec_settings_quality_get(swigCPtr);
      return ret;
    } 
  }

  public int complexity {
    set {
      freeswitchPINVOKE.switch_codec_settings_complexity_set(swigCPtr, value);
    } 
    get {
      int ret = freeswitchPINVOKE.switch_codec_settings_complexity_get(swigCPtr);
      return ret;
    } 
  }

  public int enhancement {
    set {
      freeswitchPINVOKE.switch_codec_settings_enhancement_set(swigCPtr, value);
    } 
    get {
      int ret = freeswitchPINVOKE.switch_codec_settings_enhancement_get(swigCPtr);
      return ret;
    } 
  }

  public int vad {
    set {
      freeswitchPINVOKE.switch_codec_settings_vad_set(swigCPtr, value);
    } 
    get {
      int ret = freeswitchPINVOKE.switch_codec_settings_vad_get(swigCPtr);
      return ret;
    } 
  }

  public int vbr {
    set {
      freeswitchPINVOKE.switch_codec_settings_vbr_set(swigCPtr, value);
    } 
    get {
      int ret = freeswitchPINVOKE.switch_codec_settings_vbr_get(swigCPtr);
      return ret;
    } 
  }

  public float vbr_quality {
    set {
      freeswitchPINVOKE.switch_codec_settings_vbr_quality_set(swigCPtr, value);
    } 
    get {
      float ret = freeswitchPINVOKE.switch_codec_settings_vbr_quality_get(swigCPtr);
      return ret;
    } 
  }

  public int abr {
    set {
      freeswitchPINVOKE.switch_codec_settings_abr_set(swigCPtr, value);
    } 
    get {
      int ret = freeswitchPINVOKE.switch_codec_settings_abr_get(swigCPtr);
      return ret;
    } 
  }

  public int dtx {
    set {
      freeswitchPINVOKE.switch_codec_settings_dtx_set(swigCPtr, value);
    } 
    get {
      int ret = freeswitchPINVOKE.switch_codec_settings_dtx_get(swigCPtr);
      return ret;
    } 
  }

  public int preproc {
    set {
      freeswitchPINVOKE.switch_codec_settings_preproc_set(swigCPtr, value);
    } 
    get {
      int ret = freeswitchPINVOKE.switch_codec_settings_preproc_get(swigCPtr);
      return ret;
    } 
  }

  public int pp_vad {
    set {
      freeswitchPINVOKE.switch_codec_settings_pp_vad_set(swigCPtr, value);
    } 
    get {
      int ret = freeswitchPINVOKE.switch_codec_settings_pp_vad_get(swigCPtr);
      return ret;
    } 
  }

  public int pp_agc {
    set {
      freeswitchPINVOKE.switch_codec_settings_pp_agc_set(swigCPtr, value);
    } 
    get {
      int ret = freeswitchPINVOKE.switch_codec_settings_pp_agc_get(swigCPtr);
      return ret;
    } 
  }

  public float pp_agc_level {
    set {
      freeswitchPINVOKE.switch_codec_settings_pp_agc_level_set(swigCPtr, value);
    } 
    get {
      float ret = freeswitchPINVOKE.switch_codec_settings_pp_agc_level_get(swigCPtr);
      return ret;
    } 
  }

  public int pp_denoise {
    set {
      freeswitchPINVOKE.switch_codec_settings_pp_denoise_set(swigCPtr, value);
    } 
    get {
      int ret = freeswitchPINVOKE.switch_codec_settings_pp_denoise_get(swigCPtr);
      return ret;
    } 
  }

  public int pp_dereverb {
    set {
      freeswitchPINVOKE.switch_codec_settings_pp_dereverb_set(swigCPtr, value);
    } 
    get {
      int ret = freeswitchPINVOKE.switch_codec_settings_pp_dereverb_get(swigCPtr);
      return ret;
    } 
  }

  public float pp_dereverb_decay {
    set {
      freeswitchPINVOKE.switch_codec_settings_pp_dereverb_decay_set(swigCPtr, value);
    } 
    get {
      float ret = freeswitchPINVOKE.switch_codec_settings_pp_dereverb_decay_get(swigCPtr);
      return ret;
    } 
  }

  public float pp_dereverb_level {
    set {
      freeswitchPINVOKE.switch_codec_settings_pp_dereverb_level_set(swigCPtr, value);
    } 
    get {
      float ret = freeswitchPINVOKE.switch_codec_settings_pp_dereverb_level_get(swigCPtr);
      return ret;
    } 
  }

  public switch_codec_settings() : this(freeswitchPINVOKE.new_switch_codec_settings(), true) {
  }

}

}
