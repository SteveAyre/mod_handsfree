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

public class switch_core_thread_session : IDisposable {
  private HandleRef swigCPtr;
  protected bool swigCMemOwn;

  internal switch_core_thread_session(IntPtr cPtr, bool cMemoryOwn) {
    swigCMemOwn = cMemoryOwn;
    swigCPtr = new HandleRef(this, cPtr);
  }

  internal static HandleRef getCPtr(switch_core_thread_session obj) {
    return (obj == null) ? new HandleRef(null, IntPtr.Zero) : obj.swigCPtr;
  }

  ~switch_core_thread_session() {
    Dispose();
  }

  public virtual void Dispose() {
    lock(this) {
      if(swigCPtr.Handle != IntPtr.Zero && swigCMemOwn) {
        swigCMemOwn = false;
        freeswitchPINVOKE.delete_switch_core_thread_session(swigCPtr);
      }
      swigCPtr = new HandleRef(null, IntPtr.Zero);
      GC.SuppressFinalize(this);
    }
  }

  public int running {
    set {
      freeswitchPINVOKE.switch_core_thread_session_running_set(swigCPtr, value);
    } 
    get {
      int ret = freeswitchPINVOKE.switch_core_thread_session_running_get(swigCPtr);
      return ret;
    } 
  }

  public SWIGTYPE_p_switch_mutex_t mutex {
    set {
      freeswitchPINVOKE.switch_core_thread_session_mutex_set(swigCPtr, SWIGTYPE_p_switch_mutex_t.getCPtr(value));
    } 
    get {
      IntPtr cPtr = freeswitchPINVOKE.switch_core_thread_session_mutex_get(swigCPtr);
      SWIGTYPE_p_switch_mutex_t ret = (cPtr == IntPtr.Zero) ? null : new SWIGTYPE_p_switch_mutex_t(cPtr, false);
      return ret;
    } 
  }

  public SWIGTYPE_p_p_void objs {
    set {
      freeswitchPINVOKE.switch_core_thread_session_objs_set(swigCPtr, SWIGTYPE_p_p_void.getCPtr(value));
    } 
    get {
      IntPtr cPtr = freeswitchPINVOKE.switch_core_thread_session_objs_get(swigCPtr);
      SWIGTYPE_p_p_void ret = (cPtr == IntPtr.Zero) ? null : new SWIGTYPE_p_p_void(cPtr, false);
      return ret;
    } 
  }

  public SWIGTYPE_p_f_p_switch_core_session_p_void_enum_switch_input_type_t_p_void_unsigned_int__switch_status_t input_callback {
    set {
      freeswitchPINVOKE.switch_core_thread_session_input_callback_set(swigCPtr, SWIGTYPE_p_f_p_switch_core_session_p_void_enum_switch_input_type_t_p_void_unsigned_int__switch_status_t.getCPtr(value));
    } 
    get {
      IntPtr cPtr = freeswitchPINVOKE.switch_core_thread_session_input_callback_get(swigCPtr);
      SWIGTYPE_p_f_p_switch_core_session_p_void_enum_switch_input_type_t_p_void_unsigned_int__switch_status_t ret = (cPtr == IntPtr.Zero) ? null : new SWIGTYPE_p_f_p_switch_core_session_p_void_enum_switch_input_type_t_p_void_unsigned_int__switch_status_t(cPtr, false);
      return ret;
    } 
  }

  public SWIGTYPE_p_apr_pool_t pool {
    set {
      freeswitchPINVOKE.switch_core_thread_session_pool_set(swigCPtr, SWIGTYPE_p_apr_pool_t.getCPtr(value));
    } 
    get {
      IntPtr cPtr = freeswitchPINVOKE.switch_core_thread_session_pool_get(swigCPtr);
      SWIGTYPE_p_apr_pool_t ret = (cPtr == IntPtr.Zero) ? null : new SWIGTYPE_p_apr_pool_t(cPtr, false);
      return ret;
    } 
  }

  public switch_core_thread_session() : this(freeswitchPINVOKE.new_switch_core_thread_session(), true) {
  }

}

}
