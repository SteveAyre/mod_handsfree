/* ----------------------------------------------------------------------------
 * This file was automatically generated by SWIG (http://www.swig.org).
 * Version 1.3.36
 *
 * Do not make changes to this file unless you know what you are doing--modify
 * the SWIG interface file instead.
 * ----------------------------------------------------------------------------- */

namespace FreeSWITCH.Native {

[System.Flags] public enum switch_media_bug_flag_enum_t {
  SMBF_BOTH = 0,
  SMBF_READ_STREAM = (1 << 0),
  SMBF_WRITE_STREAM = (1 << 1),
  SMBF_WRITE_REPLACE = (1 << 2),
  SMBF_READ_REPLACE = (1 << 3),
  SMBF_READ_PING = (1 << 4),
  SMBF_STEREO = (1 << 5),
  SMBF_RECORD_ANSWER_REQ = (1 << 6),
  SMBF_THREAD_LOCK = (1 << 7)
}

}
