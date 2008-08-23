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

public class switch_io_event_hook_receive_event : IDisposable {
  private HandleRef swigCPtr;
  protected bool swigCMemOwn;

  internal switch_io_event_hook_receive_event(IntPtr cPtr, bool cMemoryOwn) {
    swigCMemOwn = cMemoryOwn;
    swigCPtr = new HandleRef(this, cPtr);
  }

  internal static HandleRef getCPtr(switch_io_event_hook_receive_event obj) {
    return (obj == null) ? new HandleRef(null, IntPtr.Zero) : obj.swigCPtr;
  }

  ~switch_io_event_hook_receive_event() {
    Dispose();
  }

  public virtual void Dispose() {
    lock(this) {
      if(swigCPtr.Handle != IntPtr.Zero && swigCMemOwn) {
        swigCMemOwn = false;
        freeswitchPINVOKE.delete_switch_io_event_hook_receive_event(swigCPtr);
      }
      swigCPtr = new HandleRef(null, IntPtr.Zero);
      GC.SuppressFinalize(this);
    }
  }

  public SWIGTYPE_p_f_p_switch_core_session_p_switch_event__switch_status_t receive_event {
    set {
      freeswitchPINVOKE.switch_io_event_hook_receive_event_receive_event_set(swigCPtr, SWIGTYPE_p_f_p_switch_core_session_p_switch_event__switch_status_t.getCPtr(value));
    } 
    get {
      IntPtr cPtr = freeswitchPINVOKE.switch_io_event_hook_receive_event_receive_event_get(swigCPtr);
      SWIGTYPE_p_f_p_switch_core_session_p_switch_event__switch_status_t ret = (cPtr == IntPtr.Zero) ? null : new SWIGTYPE_p_f_p_switch_core_session_p_switch_event__switch_status_t(cPtr, false);
      return ret;
    } 
  }

  public switch_io_event_hook_receive_event next {
    set {
      freeswitchPINVOKE.switch_io_event_hook_receive_event_next_set(swigCPtr, switch_io_event_hook_receive_event.getCPtr(value));
    } 
    get {
      IntPtr cPtr = freeswitchPINVOKE.switch_io_event_hook_receive_event_next_get(swigCPtr);
      switch_io_event_hook_receive_event ret = (cPtr == IntPtr.Zero) ? null : new switch_io_event_hook_receive_event(cPtr, false);
      return ret;
    } 
  }

  public switch_io_event_hook_receive_event() : this(freeswitchPINVOKE.new_switch_io_event_hook_receive_event(), true) {
  }

}

}
