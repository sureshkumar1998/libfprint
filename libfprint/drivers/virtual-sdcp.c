/*
 * Virtual driver for SDCP device debugging
 *
 * Copyright (C) 2020 Benjamin Berg <bberg@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * This is a virtual test driver to test the basic SDCP functionality.
 * It uses the test binaries from Microsoft, which were extended to allow
 * a simple chat with the device.
 * The environment variable contains the to be executed binary including
 * arguments. This binary should be compiled from the code in
 *   https://github.com/Microsoft/SecureDeviceConnectionProtocol
 * or, until it is merged upstream
 *   https://github.com/benzea/SecureDeviceConnectionProtocol
 *
 * Note that using this as an external executable has the advantage that we
 * do not need to link against mbedtls or any other crypto library.
 */

#define FP_COMPONENT "virtual_sdcp"

#include "fpi-log.h"
#include "fpi-ssm.h"

#include "../fpi-sdcp-device.h"

#include <glib/gstdio.h>
#include <gio/gio.h>

struct _FpDeviceVirtualSdcp
{
  FpSdcpDevice   parent;

  GSubprocess   *proc;
  GOutputStream *proc_stdin;
  GInputStream  *proc_stdout;

  /* Only valid while a read/write is pending */
  GByteArray *msg_out;
  GByteArray *msg_in;

  GByteArray *connect_msg;
};

G_DECLARE_FINAL_TYPE (FpDeviceVirtualSdcp, fpi_device_virtual_sdcp, FPI, DEVICE_VIRTUAL_SDCP, FpSdcpDevice)
G_DEFINE_TYPE (FpDeviceVirtualSdcp, fpi_device_virtual_sdcp, FP_TYPE_SDCP_DEVICE)

guint8 ca_1[] = {
  0x30, 0x82, 0x03, 0xFD, 0x30, 0x82, 0x03, 0x82, 0xA0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x13, 0x33,
  0x00, 0x00, 0x00, 0x07, 0xE8, 0x9D, 0x61, 0x62, 0x4D, 0x46, 0x0F, 0x95, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x07, 0x30, 0x0A, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02, 0x30, 0x81,
  0x84, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31, 0x13,
  0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x08, 0x13, 0x0A, 0x57, 0x61, 0x73, 0x68, 0x69, 0x6E, 0x67,
  0x74, 0x6F, 0x6E, 0x31, 0x10, 0x30, 0x0E, 0x06, 0x03, 0x55, 0x04, 0x07, 0x13, 0x07, 0x52, 0x65,
  0x64, 0x6D, 0x6F, 0x6E, 0x64, 0x31, 0x1E, 0x30, 0x1C, 0x06, 0x03, 0x55, 0x04, 0x0A, 0x13, 0x15,
  0x4D, 0x69, 0x63, 0x72, 0x6F, 0x73, 0x6F, 0x66, 0x74, 0x20, 0x43, 0x6F, 0x72, 0x70, 0x6F, 0x72,
  0x61, 0x74, 0x69, 0x6F, 0x6E, 0x31, 0x2E, 0x30, 0x2C, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x25,
  0x57, 0x69, 0x6E, 0x64, 0x6F, 0x77, 0x73, 0x20, 0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x53, 0x65,
  0x63, 0x75, 0x72, 0x65, 0x20, 0x44, 0x65, 0x76, 0x69, 0x63, 0x65, 0x73, 0x20, 0x50, 0x43, 0x41,
  0x20, 0x32, 0x30, 0x31, 0x38, 0x30, 0x1E, 0x17, 0x0D, 0x31, 0x38, 0x30, 0x31, 0x33, 0x31, 0x31,
  0x39, 0x35, 0x34, 0x35, 0x33, 0x5A, 0x17, 0x0D, 0x32, 0x38, 0x30, 0x31, 0x33, 0x31, 0x32, 0x30,
  0x30, 0x34, 0x35, 0x33, 0x5A, 0x30, 0x7D, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06,
  0x13, 0x02, 0x55, 0x53, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x08, 0x13, 0x0A, 0x57,
  0x61, 0x73, 0x68, 0x69, 0x6E, 0x67, 0x74, 0x6F, 0x6E, 0x31, 0x10, 0x30, 0x0E, 0x06, 0x03, 0x55,
  0x04, 0x07, 0x13, 0x07, 0x52, 0x65, 0x64, 0x6D, 0x6F, 0x6E, 0x64, 0x31, 0x1E, 0x30, 0x1C, 0x06,
  0x03, 0x55, 0x04, 0x0A, 0x13, 0x15, 0x4D, 0x69, 0x63, 0x72, 0x6F, 0x73, 0x6F, 0x66, 0x74, 0x20,
  0x43, 0x6F, 0x72, 0x70, 0x6F, 0x72, 0x61, 0x74, 0x69, 0x6F, 0x6E, 0x31, 0x27, 0x30, 0x25, 0x06,
  0x03, 0x55, 0x04, 0x03, 0x13, 0x1E, 0x57, 0x69, 0x6E, 0x64, 0x6F, 0x77, 0x73, 0x20, 0x48, 0x65,
  0x6C, 0x6C, 0x6F, 0x20, 0x31, 0x39, 0x42, 0x39, 0x32, 0x39, 0x36, 0x35, 0x20, 0x43, 0x41, 0x20,
  0x32, 0x30, 0x31, 0x38, 0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02,
  0x01, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04, 0xBE,
  0x4B, 0x90, 0x6E, 0x24, 0xFC, 0xA1, 0x53, 0xC8, 0xA7, 0x3C, 0x70, 0xE8, 0x97, 0xCD, 0x1B, 0x31,
  0xE4, 0x95, 0x91, 0x7A, 0x58, 0xA2, 0x86, 0xA8, 0x70, 0xF6, 0x09, 0x30, 0x77, 0x99, 0x3D, 0x10,
  0xDF, 0xF7, 0x95, 0x0F, 0x68, 0x83, 0xE6, 0xA4, 0x11, 0x7C, 0xDA, 0x82, 0xE7, 0x0B, 0x8B, 0xF2,
  0x9D, 0x6B, 0x5B, 0xF5, 0x3E, 0x77, 0xB4, 0xC1, 0x0E, 0x49, 0x00, 0x83, 0xBA, 0x94, 0xF8, 0xA3,
  0x82, 0x01, 0xD7, 0x30, 0x82, 0x01, 0xD3, 0x30, 0x10, 0x06, 0x09, 0x2B, 0x06, 0x01, 0x04, 0x01,
  0x82, 0x37, 0x15, 0x01, 0x04, 0x03, 0x02, 0x01, 0x00, 0x30, 0x1D, 0x06, 0x03, 0x55, 0x1D, 0x0E,
  0x04, 0x16, 0x04, 0x14, 0x13, 0x93, 0xC8, 0xCD, 0xF2, 0x23, 0x9A, 0x2D, 0xC6, 0x9B, 0x2A, 0xEB,
  0x9A, 0xAB, 0x99, 0x0B, 0x56, 0x04, 0x5E, 0x7C, 0x30, 0x65, 0x06, 0x03, 0x55, 0x1D, 0x20, 0x04,
  0x5E, 0x30, 0x5C, 0x30, 0x06, 0x06, 0x04, 0x55, 0x1D, 0x20, 0x00, 0x30, 0x52, 0x06, 0x0C, 0x2B,
  0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x4C, 0x83, 0x7D, 0x01, 0x01, 0x30, 0x42, 0x30, 0x40, 0x06,
  0x08, 0x2B, 0x06, 0x01, 0x05, 0x05, 0x07, 0x02, 0x01, 0x16, 0x34, 0x68, 0x74, 0x74, 0x70, 0x3A,
  0x2F, 0x2F, 0x77, 0x77, 0x77, 0x2E, 0x6D, 0x69, 0x63, 0x72, 0x6F, 0x73, 0x6F, 0x66, 0x74, 0x2E,
  0x63, 0x6F, 0x6D, 0x2F, 0x70, 0x6B, 0x69, 0x6F, 0x70, 0x73, 0x2F, 0x44, 0x6F, 0x63, 0x73, 0x2F,
  0x52, 0x65, 0x70, 0x6F, 0x73, 0x69, 0x74, 0x6F, 0x72, 0x79, 0x2E, 0x68, 0x74, 0x6D, 0x00, 0x30,
  0x19, 0x06, 0x09, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x14, 0x02, 0x04, 0x0C, 0x1E, 0x0A,
  0x00, 0x53, 0x00, 0x75, 0x00, 0x62, 0x00, 0x43, 0x00, 0x41, 0x30, 0x0B, 0x06, 0x03, 0x55, 0x1D,
  0x0F, 0x04, 0x04, 0x03, 0x02, 0x01, 0x86, 0x30, 0x0F, 0x06, 0x03, 0x55, 0x1D, 0x13, 0x01, 0x01,
  0xFF, 0x04, 0x05, 0x30, 0x03, 0x01, 0x01, 0xFF, 0x30, 0x1F, 0x06, 0x03, 0x55, 0x1D, 0x23, 0x04,
  0x18, 0x30, 0x16, 0x80, 0x14, 0xDA, 0xCA, 0x4B, 0xD0, 0x4C, 0x56, 0x03, 0x27, 0x5F, 0x97, 0xEB,
  0x75, 0xA3, 0x02, 0xC3, 0xBF, 0x45, 0x9C, 0xF8, 0xB1, 0x30, 0x68, 0x06, 0x03, 0x55, 0x1D, 0x1F,
  0x04, 0x61, 0x30, 0x5F, 0x30, 0x5D, 0xA0, 0x5B, 0xA0, 0x59, 0x86, 0x57, 0x68, 0x74, 0x74, 0x70,
  0x3A, 0x2F, 0x2F, 0x77, 0x77, 0x77, 0x2E, 0x6D, 0x69, 0x63, 0x72, 0x6F, 0x73, 0x6F, 0x66, 0x74,
  0x2E, 0x63, 0x6F, 0x6D, 0x2F, 0x70, 0x6B, 0x69, 0x6F, 0x70, 0x73, 0x2F, 0x63, 0x72, 0x6C, 0x2F,
  0x57, 0x69, 0x6E, 0x64, 0x6F, 0x77, 0x73, 0x25, 0x32, 0x30, 0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x25,
  0x32, 0x30, 0x53, 0x65, 0x63, 0x75, 0x72, 0x65, 0x25, 0x32, 0x30, 0x44, 0x65, 0x76, 0x69, 0x63,
  0x65, 0x73, 0x25, 0x32, 0x30, 0x50, 0x43, 0x41, 0x25, 0x32, 0x30, 0x32, 0x30, 0x31, 0x38, 0x2E,
  0x63, 0x72, 0x6C, 0x30, 0x75, 0x06, 0x08, 0x2B, 0x06, 0x01, 0x05, 0x05, 0x07, 0x01, 0x01, 0x04,
  0x69, 0x30, 0x67, 0x30, 0x65, 0x06, 0x08, 0x2B, 0x06, 0x01, 0x05, 0x05, 0x07, 0x30, 0x02, 0x86,
  0x59, 0x68, 0x74, 0x74, 0x70, 0x3A, 0x2F, 0x2F, 0x77, 0x77, 0x77, 0x2E, 0x6D, 0x69, 0x63, 0x72,
  0x6F, 0x73, 0x6F, 0x66, 0x74, 0x2E, 0x63, 0x6F, 0x6D, 0x2F, 0x70, 0x6B, 0x69, 0x6F, 0x70, 0x73,
  0x2F, 0x63, 0x65, 0x72, 0x74, 0x73, 0x2F, 0x57, 0x69, 0x6E, 0x64, 0x6F, 0x77, 0x73, 0x25, 0x32,
  0x30, 0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x25, 0x32, 0x30, 0x53, 0x65, 0x63, 0x75, 0x72, 0x65, 0x25,
  0x32, 0x30, 0x44, 0x65, 0x76, 0x69, 0x63, 0x65, 0x73, 0x25, 0x32, 0x30, 0x50, 0x43, 0x41, 0x25,
  0x32, 0x30, 0x32, 0x30, 0x31, 0x38, 0x2E, 0x63, 0x72, 0x74, 0x30, 0x0A, 0x06, 0x08, 0x2A, 0x86,
  0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02, 0x03, 0x69, 0x00, 0x30, 0x66, 0x02, 0x31, 0x00, 0x87, 0xB6,
  0x82, 0xF3, 0xDA, 0xBE, 0xB1, 0x7B, 0x98, 0x7D, 0x3D, 0x0A, 0x90, 0xA8, 0xF5, 0xBF, 0x15, 0xC3,
  0xEE, 0x8A, 0x4E, 0xC0, 0x7B, 0x10, 0x1D, 0xA9, 0xE3, 0x0B, 0xEC, 0x2C, 0x53, 0x4E, 0xA7, 0xBD,
  0xF1, 0x6C, 0xAD, 0x18, 0x55, 0xBA, 0x25, 0x73, 0x55, 0xB7, 0x5B, 0x12, 0x24, 0xF4, 0x02, 0x31,
  0x00, 0xAF, 0x02, 0x9C, 0x4B, 0x92, 0xD0, 0x72, 0xA5, 0x80, 0xCA, 0x69, 0x2B, 0x38, 0x50, 0x64,
  0xD8, 0x58, 0x9E, 0xEA, 0xD6, 0x35, 0xCF, 0x68, 0x98, 0x92, 0x81, 0x09, 0x61, 0xC2, 0xBD, 0xB1,
  0x4C, 0x7F, 0xAE, 0x55, 0x7B, 0xFC, 0x22, 0xDD, 0xD6, 0xB7, 0x7C, 0xB5, 0xA8, 0x18, 0x5D, 0x33,
  0x04
};
guint8 ca_2[] = {
  0x30, 0x82, 0x04, 0x56, 0x30, 0x82, 0x03, 0xDC, 0xA0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x13, 0x33,
  0x00, 0x00, 0x00, 0x03, 0x6C, 0xCF, 0xED, 0xE2, 0x44, 0x70, 0x19, 0xBF, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x03, 0x30, 0x0A, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x03, 0x30, 0x81,
  0x94, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31, 0x13,
  0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x08, 0x13, 0x0A, 0x57, 0x61, 0x73, 0x68, 0x69, 0x6E, 0x67,
  0x74, 0x6F, 0x6E, 0x31, 0x10, 0x30, 0x0E, 0x06, 0x03, 0x55, 0x04, 0x07, 0x13, 0x07, 0x52, 0x65,
  0x64, 0x6D, 0x6F, 0x6E, 0x64, 0x31, 0x1E, 0x30, 0x1C, 0x06, 0x03, 0x55, 0x04, 0x0A, 0x13, 0x15,
  0x4D, 0x69, 0x63, 0x72, 0x6F, 0x73, 0x6F, 0x66, 0x74, 0x20, 0x43, 0x6F, 0x72, 0x70, 0x6F, 0x72,
  0x61, 0x74, 0x69, 0x6F, 0x6E, 0x31, 0x3E, 0x30, 0x3C, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x35,
  0x4D, 0x69, 0x63, 0x72, 0x6F, 0x73, 0x6F, 0x66, 0x74, 0x20, 0x45, 0x43, 0x43, 0x20, 0x44, 0x65,
  0x76, 0x69, 0x63, 0x65, 0x73, 0x20, 0x52, 0x6F, 0x6F, 0x74, 0x20, 0x43, 0x65, 0x72, 0x74, 0x69,
  0x66, 0x69, 0x63, 0x61, 0x74, 0x65, 0x20, 0x41, 0x75, 0x74, 0x68, 0x6F, 0x72, 0x69, 0x74, 0x79,
  0x20, 0x32, 0x30, 0x31, 0x37, 0x30, 0x1E, 0x17, 0x0D, 0x31, 0x38, 0x30, 0x31, 0x32, 0x35, 0x31,
  0x39, 0x34, 0x39, 0x33, 0x38, 0x5A, 0x17, 0x0D, 0x33, 0x33, 0x30, 0x31, 0x32, 0x35, 0x31, 0x39,
  0x35, 0x39, 0x33, 0x38, 0x5A, 0x30, 0x81, 0x84, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04,
  0x06, 0x13, 0x02, 0x55, 0x53, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x08, 0x13, 0x0A,
  0x57, 0x61, 0x73, 0x68, 0x69, 0x6E, 0x67, 0x74, 0x6F, 0x6E, 0x31, 0x10, 0x30, 0x0E, 0x06, 0x03,
  0x55, 0x04, 0x07, 0x13, 0x07, 0x52, 0x65, 0x64, 0x6D, 0x6F, 0x6E, 0x64, 0x31, 0x1E, 0x30, 0x1C,
  0x06, 0x03, 0x55, 0x04, 0x0A, 0x13, 0x15, 0x4D, 0x69, 0x63, 0x72, 0x6F, 0x73, 0x6F, 0x66, 0x74,
  0x20, 0x43, 0x6F, 0x72, 0x70, 0x6F, 0x72, 0x61, 0x74, 0x69, 0x6F, 0x6E, 0x31, 0x2E, 0x30, 0x2C,
  0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x25, 0x57, 0x69, 0x6E, 0x64, 0x6F, 0x77, 0x73, 0x20, 0x48,
  0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x53, 0x65, 0x63, 0x75, 0x72, 0x65, 0x20, 0x44, 0x65, 0x76, 0x69,
  0x63, 0x65, 0x73, 0x20, 0x50, 0x43, 0x41, 0x20, 0x32, 0x30, 0x31, 0x38, 0x30, 0x76, 0x30, 0x10,
  0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01, 0x06, 0x05, 0x2B, 0x81, 0x04, 0x00, 0x22,
  0x03, 0x62, 0x00, 0x04, 0x1D, 0xDD, 0x08, 0x02, 0x03, 0x25, 0x75, 0x20, 0xE2, 0x71, 0x8B, 0xAD,
  0x28, 0x09, 0x82, 0xE9, 0x06, 0xEE, 0x83, 0xC5, 0x3A, 0x6C, 0x4B, 0x71, 0x92, 0x50, 0x4E, 0x20,
  0xE9, 0x72, 0xB4, 0xFC, 0x53, 0x2A, 0xEF, 0x5D, 0xCC, 0x9A, 0xB4, 0xCD, 0x76, 0xB8, 0x94, 0x97,
  0x44, 0xB2, 0x71, 0x0E, 0xC9, 0xB1, 0x16, 0x03, 0xA1, 0x65, 0x2B, 0xB9, 0xE8, 0x5D, 0x5F, 0xF2,
  0x30, 0x2E, 0xDD, 0xB1, 0x2B, 0x20, 0xFC, 0xBE, 0x00, 0x88, 0xEA, 0x1F, 0xA7, 0x7F, 0x99, 0x84,
  0x98, 0x7C, 0x71, 0x3E, 0x4D, 0x34, 0x83, 0x69, 0x9B, 0x08, 0xCB, 0x78, 0xB2, 0x4B, 0xBD, 0xD7,
  0x3E, 0xBE, 0x67, 0xA0, 0xA3, 0x82, 0x01, 0xFC, 0x30, 0x82, 0x01, 0xF8, 0x30, 0x10, 0x06, 0x09,
  0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x15, 0x01, 0x04, 0x03, 0x02, 0x01, 0x00, 0x30, 0x1D,
  0x06, 0x03, 0x55, 0x1D, 0x0E, 0x04, 0x16, 0x04, 0x14, 0xDA, 0xCA, 0x4B, 0xD0, 0x4C, 0x56, 0x03,
  0x27, 0x5F, 0x97, 0xEB, 0x75, 0xA3, 0x02, 0xC3, 0xBF, 0x45, 0x9C, 0xF8, 0xB1, 0x30, 0x65, 0x06,
  0x03, 0x55, 0x1D, 0x20, 0x04, 0x5E, 0x30, 0x5C, 0x30, 0x06, 0x06, 0x04, 0x55, 0x1D, 0x20, 0x00,
  0x30, 0x52, 0x06, 0x0C, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x4C, 0x83, 0x7D, 0x01, 0x01,
  0x30, 0x42, 0x30, 0x40, 0x06, 0x08, 0x2B, 0x06, 0x01, 0x05, 0x05, 0x07, 0x02, 0x01, 0x16, 0x34,
  0x68, 0x74, 0x74, 0x70, 0x3A, 0x2F, 0x2F, 0x77, 0x77, 0x77, 0x2E, 0x6D, 0x69, 0x63, 0x72, 0x6F,
  0x73, 0x6F, 0x66, 0x74, 0x2E, 0x63, 0x6F, 0x6D, 0x2F, 0x70, 0x6B, 0x69, 0x6F, 0x70, 0x73, 0x2F,
  0x44, 0x6F, 0x63, 0x73, 0x2F, 0x52, 0x65, 0x70, 0x6F, 0x73, 0x69, 0x74, 0x6F, 0x72, 0x79, 0x2E,
  0x68, 0x74, 0x6D, 0x00, 0x30, 0x19, 0x06, 0x09, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x14,
  0x02, 0x04, 0x0C, 0x1E, 0x0A, 0x00, 0x53, 0x00, 0x75, 0x00, 0x62, 0x00, 0x43, 0x00, 0x41, 0x30,
  0x0B, 0x06, 0x03, 0x55, 0x1D, 0x0F, 0x04, 0x04, 0x03, 0x02, 0x01, 0x86, 0x30, 0x0F, 0x06, 0x03,
  0x55, 0x1D, 0x13, 0x01, 0x01, 0xFF, 0x04, 0x05, 0x30, 0x03, 0x01, 0x01, 0xFF, 0x30, 0x1F, 0x06,
  0x03, 0x55, 0x1D, 0x23, 0x04, 0x18, 0x30, 0x16, 0x80, 0x14, 0x14, 0xDA, 0x5B, 0xF1, 0x0E, 0x66,
  0x47, 0xD1, 0x5D, 0x13, 0x5F, 0x5B, 0x7A, 0xEB, 0xEB, 0x5F, 0x01, 0x08, 0xB5, 0x49, 0x30, 0x7A,
  0x06, 0x03, 0x55, 0x1D, 0x1F, 0x04, 0x73, 0x30, 0x71, 0x30, 0x6F, 0xA0, 0x6D, 0xA0, 0x6B, 0x86,
  0x69, 0x68, 0x74, 0x74, 0x70, 0x3A, 0x2F, 0x2F, 0x77, 0x77, 0x77, 0x2E, 0x6D, 0x69, 0x63, 0x72,
  0x6F, 0x73, 0x6F, 0x66, 0x74, 0x2E, 0x63, 0x6F, 0x6D, 0x2F, 0x70, 0x6B, 0x69, 0x6F, 0x70, 0x73,
  0x2F, 0x63, 0x72, 0x6C, 0x2F, 0x4D, 0x69, 0x63, 0x72, 0x6F, 0x73, 0x6F, 0x66, 0x74, 0x25, 0x32,
  0x30, 0x45, 0x43, 0x43, 0x25, 0x32, 0x30, 0x44, 0x65, 0x76, 0x69, 0x63, 0x65, 0x73, 0x25, 0x32,
  0x30, 0x52, 0x6F, 0x6F, 0x74, 0x25, 0x32, 0x30, 0x43, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63,
  0x61, 0x74, 0x65, 0x25, 0x32, 0x30, 0x41, 0x75, 0x74, 0x68, 0x6F, 0x72, 0x69, 0x74, 0x79, 0x25,
  0x32, 0x30, 0x32, 0x30, 0x31, 0x37, 0x2E, 0x63, 0x72, 0x6C, 0x30, 0x81, 0x87, 0x06, 0x08, 0x2B,
  0x06, 0x01, 0x05, 0x05, 0x07, 0x01, 0x01, 0x04, 0x7B, 0x30, 0x79, 0x30, 0x77, 0x06, 0x08, 0x2B,
  0x06, 0x01, 0x05, 0x05, 0x07, 0x30, 0x02, 0x86, 0x6B, 0x68, 0x74, 0x74, 0x70, 0x3A, 0x2F, 0x2F,
  0x77, 0x77, 0x77, 0x2E, 0x6D, 0x69, 0x63, 0x72, 0x6F, 0x73, 0x6F, 0x66, 0x74, 0x2E, 0x63, 0x6F,
  0x6D, 0x2F, 0x70, 0x6B, 0x69, 0x6F, 0x70, 0x73, 0x2F, 0x63, 0x65, 0x72, 0x74, 0x73, 0x2F, 0x4D,
  0x69, 0x63, 0x72, 0x6F, 0x73, 0x6F, 0x66, 0x74, 0x25, 0x32, 0x30, 0x45, 0x43, 0x43, 0x25, 0x32,
  0x30, 0x44, 0x65, 0x76, 0x69, 0x63, 0x65, 0x73, 0x25, 0x32, 0x30, 0x52, 0x6F, 0x6F, 0x74, 0x25,
  0x32, 0x30, 0x43, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63, 0x61, 0x74, 0x65, 0x25, 0x32, 0x30,
  0x41, 0x75, 0x74, 0x68, 0x6F, 0x72, 0x69, 0x74, 0x79, 0x25, 0x32, 0x30, 0x32, 0x30, 0x31, 0x37,
  0x2E, 0x63, 0x72, 0x74, 0x30, 0x0A, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x03,
  0x03, 0x68, 0x00, 0x30, 0x65, 0x02, 0x30, 0x56, 0x2A, 0xAD, 0x72, 0x4C, 0xB9, 0x8C, 0xB3, 0x23,
  0x80, 0xF5, 0x5F, 0xF8, 0x21, 0x94, 0x66, 0x0F, 0x76, 0x77, 0xE2, 0x7B, 0x03, 0xDD, 0x30, 0x5E,
  0xCB, 0x90, 0xCA, 0x78, 0xE6, 0x0B, 0x2D, 0x12, 0xE5, 0xF7, 0x67, 0x31, 0x58, 0x71, 0xE6, 0xF3,
  0x64, 0xC1, 0x04, 0xB3, 0x8B, 0xE9, 0xE2, 0x02, 0x31, 0x00, 0xB9, 0x20, 0x61, 0xB9, 0xD0, 0x5E,
  0x3A, 0xA4, 0xA2, 0x8A, 0xFE, 0x1D, 0xFC, 0x27, 0x61, 0x0B, 0x98, 0x16, 0x8C, 0x02, 0x9C, 0x20,
  0x7F, 0xEE, 0xF3, 0xCB, 0x1F, 0x0A, 0x37, 0x62, 0xB1, 0x8E, 0xCE, 0xD9, 0x9A, 0x9E, 0xAC, 0xE6,
  0x1A, 0xD4, 0xB8, 0xF1, 0xA8, 0x2B, 0xB1, 0xB4, 0x40, 0x9B
};


static void
msg_written_cb (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
  GError *error = NULL;
  GOutputStream *stream = G_OUTPUT_STREAM (source_object);
  FpiSsm *ssm = user_data;
  FpDeviceVirtualSdcp *self = FPI_DEVICE_VIRTUAL_SDCP (fpi_ssm_get_device (ssm));

  g_clear_pointer (&self->msg_out, g_byte_array_unref);
  g_assert (self->msg_out == NULL);

  if (!g_output_stream_write_all_finish (stream, res, NULL, &error))
    {
      fpi_ssm_mark_failed (ssm, error);
      return;
    }

  fpi_ssm_next_state (ssm);
}

static void
msg_received_cb (GObject      *source_object,
                 GAsyncResult *res,
                 gpointer      user_data)
{
  GError *error = NULL;
  GInputStream *stream = G_INPUT_STREAM (source_object);
  FpiSsm *ssm = user_data;
  FpDeviceVirtualSdcp *self = FPI_DEVICE_VIRTUAL_SDCP (fpi_ssm_get_device (ssm));
  gsize read;

  g_assert (self->msg_out == NULL);

  if (!g_input_stream_read_all_finish (stream, res, &read, &error) ||
      read != self->msg_in->len)
    {
      g_clear_pointer (&self->msg_in, g_byte_array_unref);

      if (!error)
        error = fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                          "Received EOF while reading from test binary.");

      fpi_ssm_mark_failed (ssm, error);
      return;
    }

  fpi_ssm_next_state (ssm);
}

enum {
  SEND_MESSAGE,
  RECV_MESSAGE,
  SEND_RECV_STATES
};

static void
send_recv_ssm (FpiSsm *ssm, FpDevice *dev)
{
  FpDeviceVirtualSdcp *self = FPI_DEVICE_VIRTUAL_SDCP (dev);

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case SEND_MESSAGE:
      g_output_stream_write_all_async (self->proc_stdin,
                                       self->msg_out->data,
                                       self->msg_out->len,
                                       G_PRIORITY_DEFAULT,
                                       fpi_device_get_cancellable (FP_DEVICE (dev)),
                                       msg_written_cb,
                                       ssm);
      break;

    case RECV_MESSAGE:
      g_input_stream_read_all_async (self->proc_stdout,
                                     self->msg_in->data,
                                     self->msg_in->len,
                                     G_PRIORITY_DEFAULT,
                                     fpi_device_get_cancellable (FP_DEVICE (dev)),
                                     msg_received_cb,
                                     ssm);
      break;
    }
}

static void
connect_2_cb (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  g_autoptr(GBytes) recv_data = NULL;
  g_autoptr(GBytes) r_d = NULL;
  g_autoptr(FpiSdcpClaim) claim = NULL;
  g_autoptr(GBytes) mac = NULL;
  g_autoptr(GBytes) ca_1_bytes = NULL, ca_2_bytes = NULL;
  FpDeviceVirtualSdcp *self = FPI_DEVICE_VIRTUAL_SDCP (dev);
  guint16 cert_size;

  if (error)
    {
      fpi_sdcp_device_connect_complete (FP_SDCP_DEVICE (dev), NULL, NULL, NULL, error);
      g_clear_pointer (&self->connect_msg, g_byte_array_unref);
      return;
    }

  memcpy (&cert_size, self->connect_msg->data + 32, 2);
  g_byte_array_append (self->connect_msg, self->msg_in->data, self->msg_in->len);
  g_clear_pointer (&self->msg_in, g_byte_array_unref);
  /* Double check that the size is correct. */
  g_assert (self->connect_msg->len == 32 + (2 + cert_size + 65 + 65 + 32 + 64 + 64) + 32);
  recv_data = g_byte_array_free_to_bytes (g_steal_pointer (&self->connect_msg));

  claim = fpi_sdcp_claim_new ();
  r_d = g_bytes_new_from_bytes (recv_data, 0, 32);
  claim->cert_m = g_bytes_new_from_bytes (recv_data, 34, cert_size);
  claim->pk_d = g_bytes_new_from_bytes (recv_data, 34 + cert_size, 65);
  claim->pk_f = g_bytes_new_from_bytes (recv_data, 34 + cert_size + 65, 65);
  claim->h_f = g_bytes_new_from_bytes (recv_data, 34 + cert_size + 65 + 65, 32);
  claim->s_m = g_bytes_new_from_bytes (recv_data, 34 + cert_size + 65 + 65 + 32, 64);
  claim->s_d = g_bytes_new_from_bytes (recv_data, 34 + cert_size + 65 + 65 + 32 + 64, 64);
  mac = g_bytes_new_from_bytes (recv_data, 34 + cert_size + 65 + 65 + 32 + 64 + 64, 32);

  ca_1_bytes = g_bytes_new_static (ca_1, G_N_ELEMENTS (ca_1));
  ca_2_bytes = g_bytes_new_static (ca_2, G_N_ELEMENTS (ca_2));

  fpi_sdcp_device_set_intermediat_cas (FP_SDCP_DEVICE (dev),
                                       ca_1_bytes,
                                       ca_2_bytes);

  fpi_sdcp_device_connect_complete (FP_SDCP_DEVICE (dev), r_d, claim, mac, NULL);
}

static void
connect_1_cb (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  FpDeviceVirtualSdcp *self = FPI_DEVICE_VIRTUAL_SDCP (dev);
  guint16 cert_size;

  if (error)
    {
      fpi_sdcp_device_connect_complete (FP_SDCP_DEVICE (dev), NULL, NULL, NULL, error);
      return;
    }

  g_clear_pointer (&self->connect_msg, g_byte_array_unref);
  self->connect_msg = g_steal_pointer (&self->msg_in);

  memcpy (&cert_size, self->connect_msg->data + 32, 2);

  /* Nothing to send and the rest to receive. */
  self->msg_out = g_byte_array_new ();
  self->msg_in = g_byte_array_new ();
  g_byte_array_set_size (self->msg_in, 32 + (2 + cert_size + 65 + 65 + 32 + 64 + 64) + 32 - self->connect_msg->len);

  /* New SSM */
  ssm = fpi_ssm_new_full (FP_DEVICE (dev), send_recv_ssm, SEND_RECV_STATES, SEND_RECV_STATES, "connect 2");
  fpi_ssm_start (ssm, connect_2_cb);
}

static void
connect (FpSdcpDevice *dev)
{
  g_autoptr(GBytes) r_h = NULL;
  g_autoptr(GBytes) pk_h = NULL;
  FpDeviceVirtualSdcp *self = FPI_DEVICE_VIRTUAL_SDCP (dev);
  FpiSsm *ssm;

  G_DEBUG_HERE ();

  g_assert (self->proc);
  g_assert (self->connect_msg == NULL);

  fpi_sdcp_device_get_connect_data (dev, &r_h, &pk_h);

  self->msg_out = g_byte_array_new ();
  g_byte_array_append (self->msg_out, (const guint8 *) "C", 1);
  g_byte_array_append (self->msg_out,
                       g_bytes_get_data (r_h, NULL),
                       g_bytes_get_size (r_h));
  g_byte_array_append (self->msg_out,
                       g_bytes_get_data (pk_h, NULL),
                       g_bytes_get_size (pk_h));

  self->msg_in = g_byte_array_new ();
  g_byte_array_set_size (self->msg_in, 34);

  ssm = fpi_ssm_new_full (FP_DEVICE (dev), send_recv_ssm, SEND_RECV_STATES, SEND_RECV_STATES, "connect");
  fpi_ssm_start (ssm, connect_1_cb);
}

static void
reconnect_cb (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  g_autoptr(GBytes) mac = NULL;
  FpDeviceVirtualSdcp *self = FPI_DEVICE_VIRTUAL_SDCP (dev);

  if (error)
    {
      fpi_sdcp_device_reconnect_complete (FP_SDCP_DEVICE (dev), mac, error);
      return;
    }

  mac = g_byte_array_free_to_bytes (g_steal_pointer (&self->msg_in));

  fpi_sdcp_device_reconnect_complete (FP_SDCP_DEVICE (dev), mac, NULL);
}

static void
reconnect (FpSdcpDevice *dev)
{
  g_autoptr(GBytes) r_h = NULL;
  FpDeviceVirtualSdcp *self = FPI_DEVICE_VIRTUAL_SDCP (dev);
  FpiSsm *ssm;

  G_DEBUG_HERE ();

  g_assert (self->proc);

  fpi_sdcp_device_get_reconnect_data (dev, &r_h);

  self->msg_out = g_byte_array_new ();
  g_byte_array_append (self->msg_out, (const guint8 *) "R", 1);
  g_byte_array_append (self->msg_out,
                       g_bytes_get_data (r_h, NULL),
                       g_bytes_get_size (r_h));

  self->msg_in = g_byte_array_new ();
  g_byte_array_set_size (self->msg_in, 32);

  ssm = fpi_ssm_new_full (FP_DEVICE (dev), send_recv_ssm, SEND_RECV_STATES, SEND_RECV_STATES, "connect 2");
  fpi_ssm_start (ssm, reconnect_cb);
}

static void
enroll_begin_cb (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  g_autoptr(GBytes) nonce = NULL;
  FpDeviceVirtualSdcp *self = FPI_DEVICE_VIRTUAL_SDCP (dev);

  if (error)
    {
      fpi_sdcp_device_enroll_ready (FP_SDCP_DEVICE (dev), error);
      return;
    }

  nonce = g_byte_array_free_to_bytes (g_steal_pointer (&self->msg_in));

  fpi_sdcp_device_enroll_set_nonce (FP_SDCP_DEVICE (dev), nonce);

  /* Claim that we completed one enroll step. */
  fpi_device_enroll_progress (dev, 1, NULL, NULL);

  /* And signal that we are ready to commit. */
  fpi_sdcp_device_enroll_ready (FP_SDCP_DEVICE (dev), NULL);
}

static void
enroll_begin (FpSdcpDevice *dev)
{
  g_autoptr(GBytes) r_h = NULL;
  FpDeviceVirtualSdcp *self = FPI_DEVICE_VIRTUAL_SDCP (dev);
  FpiSsm *ssm;

  G_DEBUG_HERE ();

  g_assert (self->proc);

  fpi_sdcp_device_get_reconnect_data (dev, &r_h);

  self->msg_out = g_byte_array_new ();
  g_byte_array_append (self->msg_out, (const guint8 *) "E", 1);

  /* Expect 32 byte nonce */
  self->msg_in = g_byte_array_new ();
  g_byte_array_set_size (self->msg_in, 32);

  ssm = fpi_ssm_new_full (FP_DEVICE (dev), send_recv_ssm, SEND_RECV_STATES, SEND_RECV_STATES, "enroll_begin");
  fpi_ssm_start (ssm, enroll_begin_cb);
}

static void
enroll_commit_cb (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  FpDeviceVirtualSdcp *self = FPI_DEVICE_VIRTUAL_SDCP (dev);

  g_clear_pointer (&self->msg_in, g_byte_array_unref);

  if (error)
    {
      fpi_sdcp_device_enroll_ready (FP_SDCP_DEVICE (dev), error);
      return;
    }

  /* Signal that we have committed. We don't expect a response
   * from the virtual device (even though that is kind of broken).
   */
  fpi_sdcp_device_enroll_commit_complete (FP_SDCP_DEVICE (dev), NULL);
}

static void
enroll_commit (FpSdcpDevice *dev, GBytes *id_in)
{
  g_autoptr(GBytes) r_h = NULL;
  g_autoptr(GBytes) id = id_in;
  FpDeviceVirtualSdcp *self = FPI_DEVICE_VIRTUAL_SDCP (dev);
  FpiSsm *ssm;

  G_DEBUG_HERE ();

  g_assert (self->proc);

  fpi_sdcp_device_get_reconnect_data (dev, &r_h);

  self->msg_out = g_byte_array_new ();
  self->msg_in = g_byte_array_new ();
  if (id)
    {
      g_byte_array_append (self->msg_out, (const guint8 *) "F", 1);
      g_byte_array_append (self->msg_out,
                           g_bytes_get_data (id, NULL),
                           g_bytes_get_size (id));

      /* NOTE: No response from device, assume commit works. */
    }
  else
    {
      /* Cancel enroll (does not receive a reply) */
      g_byte_array_append (self->msg_out, (const guint8 *) "G", 1);

      /* NOTE: No response from device, assume cancellation works. */
    }

  ssm = fpi_ssm_new_full (FP_DEVICE (dev), send_recv_ssm, SEND_RECV_STATES, SEND_RECV_STATES, "enroll_commit");
  fpi_ssm_start (ssm, enroll_commit_cb);
}

static void
identify_cb (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  g_autoptr(GBytes) reply = NULL;
  g_autoptr(GBytes) id = NULL;
  g_autoptr(GBytes) mac = NULL;
  FpDeviceVirtualSdcp *self = FPI_DEVICE_VIRTUAL_SDCP (dev);

  if (error)
    {
      fpi_sdcp_device_identify_complete (FP_SDCP_DEVICE (dev), NULL, NULL, error);
      return;
    }

  reply = g_byte_array_free_to_bytes (g_steal_pointer (&self->msg_in));
  id = g_bytes_new_from_bytes (reply, 0, 32);
  mac = g_bytes_new_from_bytes (reply, 32, 32);

  fpi_sdcp_device_identify_complete (FP_SDCP_DEVICE (self), id, mac, NULL);
}

static void
identify (FpSdcpDevice *dev)
{
  g_autoptr(GBytes) nonce = NULL;
  FpDeviceVirtualSdcp *self = FPI_DEVICE_VIRTUAL_SDCP (dev);
  FpiSsm *ssm;

  G_DEBUG_HERE ();

  g_assert (self->proc);

  fpi_sdcp_device_get_identify_data (dev, &nonce);

  self->msg_out = g_byte_array_new ();
  g_byte_array_append (self->msg_out, (const guint8 *) "I", 1);
  g_byte_array_append (self->msg_out, g_bytes_get_data (nonce, NULL), g_bytes_get_size (nonce));

  /* Expect 64 byte nonce */
  self->msg_in = g_byte_array_new ();
  g_byte_array_set_size (self->msg_in, 64);

  ssm = fpi_ssm_new_full (FP_DEVICE (dev), send_recv_ssm, SEND_RECV_STATES, SEND_RECV_STATES, "identify");
  fpi_ssm_start (ssm, identify_cb);
}

static void
probe (FpDevice *dev)
{
  g_auto(GStrv) argv = NULL;
  FpDeviceVirtualSdcp *self = FPI_DEVICE_VIRTUAL_SDCP (dev);
  GError *error = NULL;
  const char *env;

  /* We launch the test binary alread at probe time and quit only when
   * the object is finalized. This allows testing reconnect properly.
   *
   * Also, we'll fail probe if something goes wrong executing it.
   */
  env = fpi_device_get_virtual_env (FP_DEVICE (self));

  if (!g_shell_parse_argv (env, NULL, &argv, &error))
    goto out;

  self->proc = g_subprocess_newv ((const char * const *) argv,
                                  G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE,
                                  &error);
  if (!self->proc)
    goto out;

  self->proc_stdin = g_object_ref (g_subprocess_get_stdin_pipe (self->proc));
  self->proc_stdout = g_object_ref (g_subprocess_get_stdout_pipe (self->proc));


out:
  fpi_device_probe_complete (dev, "virtual-sdcp", NULL, error);
}

static void
dev_close (FpDevice *dev)
{
  /* No-op, needs to be defined. */
  fpi_device_close_complete (dev, NULL);
}

static void
fpi_device_virtual_sdcp_init (FpDeviceVirtualSdcp *self)
{
}

static void
fpi_device_virtual_sdcp_finalize (GObject *obj)
{
  FpDeviceVirtualSdcp *self = FPI_DEVICE_VIRTUAL_SDCP (obj);

  /* Just kill the subprocess, no need to be graceful here. */
  if (self->proc)
    g_subprocess_force_exit (self->proc);

  g_clear_object (&self->proc);
  g_clear_object (&self->proc_stdin);
  g_clear_object (&self->proc_stdout);

  G_OBJECT_CLASS (fpi_device_virtual_sdcp_parent_class)->finalize (obj);
}

static const FpIdEntry driver_ids[] = {
  { .virtual_envvar = "FP_VIRTUAL_SDCP" },
  { .virtual_envvar = NULL }
};

static void
fpi_device_virtual_sdcp_class_init (FpDeviceVirtualSdcpClass *klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);
  FpSdcpDeviceClass *sdcp_class = FP_SDCP_DEVICE_CLASS (klass);

  obj_class->finalize = fpi_device_virtual_sdcp_finalize;

  dev_class->id = FP_COMPONENT;
  dev_class->full_name = "Virtual SDCP device talking to MS test code";
  dev_class->type = FP_DEVICE_TYPE_VIRTUAL;
  dev_class->id_table = driver_ids;
  dev_class->nr_enroll_stages = 1;

  /* The SDCP base class may need to override this in the long run */
  dev_class->probe = probe;
  dev_class->close = dev_close;

  sdcp_class->connect = connect;
  sdcp_class->reconnect = reconnect;

  sdcp_class->enroll_begin = enroll_begin;
  sdcp_class->enroll_commit = enroll_commit;

  sdcp_class->identify = identify;
}