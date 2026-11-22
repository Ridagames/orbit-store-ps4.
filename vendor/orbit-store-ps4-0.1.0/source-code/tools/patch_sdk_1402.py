#!/usr/bin/env python3
"""Apply a guarded PS4 14.02 kernel-init compatibility patch to the pinned SDK.

The patch only enables 14.02 when runtime-discovered kernel addresses match
values independently checked against retail firmware J02700521. It deliberately
does not guess the targetid symbol or any unverified patch offsets.
"""
from pathlib import Path
import sys

path = Path(sys.argv[1])
source = path.read_text()

def replace_once(old: str, new: str, label: str) -> None:
    global source
    count = source.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one source match, found {count}")
    source = source.replace(old, new, 1)

replace_once(
"""    if((err=kexec(kexec_find_image_base, &KERNEL_ADDRESS_IMAGE_BASE))) {
      return err;
    }
""",
"""    if((err=kexec(kexec_find_image_base, &KERNEL_ADDRESS_IMAGE_BASE))) {
      return err;
    }
    // FW 14.02 (J02700521): verified LSTAR-to-kernel-base delta.
    if(fw == 0x14020000 &&
       KERNEL_ADDRESS_IMAGE_BASE != lstar - 0x000001C0) {
      return -ENOSYS;
    }
""",
"14.02 kernel-base guard")

replace_once(
"""    if((err=kexec(kexec_find_copyout, KERNEL_ADDRESS_IMAGE_BASE,
\t\t  KERNEL_IMAGE_SIZE, &KERNEL_ADDRESS_COPYOUT))) {
      return err;
    }
""",
"""    if((err=kexec(kexec_find_copyout, KERNEL_ADDRESS_IMAGE_BASE,
\t\t  KERNEL_IMAGE_SIZE, &KERNEL_ADDRESS_COPYOUT))) {
      return err;
    }
    // Independently verified against the retail 14.02 kernel.
    if(fw == 0x14020000 &&
       KERNEL_ADDRESS_COPYOUT != KERNEL_ADDRESS_IMAGE_BASE + 0x002BDC50) {
      return -ENOSYS;
    }
""",
"14.02 copyout guard")

replace_once(
"""    if(!KERNEL_ADDRESS_IMAGE_BASE || !KERNEL_IMAGE_SIZE ||
       !KERNEL_ADDRESS_COPYIN || !KERNEL_ADDRESS_COPYOUT ||
       !KERNEL_ADDRESS_TARGETID) {
      return -ENOSYS;
    }
""",
"""    if(!KERNEL_ADDRESS_IMAGE_BASE || !KERNEL_IMAGE_SIZE ||
       !KERNEL_ADDRESS_COPYIN || !KERNEL_ADDRESS_COPYOUT ||
       (!KERNEL_ADDRESS_TARGETID && fw != 0x14020000)) {
      return -ENOSYS;
    }
    if(fw == 0x14020000 &&
       (KERNEL_ADDRESS_COPYIN < KERNEL_ADDRESS_IMAGE_BASE ||
        KERNEL_ADDRESS_COPYIN >= KERNEL_ADDRESS_IMAGE_BASE + KERNEL_IMAGE_SIZE)) {
      return -ENOSYS;
    }
""",
"allow only guarded 14.02 without targetid")

replace_once(
"""  if(!(KERNEL_ADDRESS_ROOTVNODE=kernel_get_proc_rootdir(1))) {
    return -1;
  }

  KERNEL_ADDRESS_QA_FLAGS = KERNEL_ADDRESS_TARGETID + 0x1b;
""",
"""  if(!(KERNEL_ADDRESS_ROOTVNODE=kernel_get_proc_rootdir(1))) {
    return -1;
  }

  // Guard the process-list and credential-derived globals against the
  // independently verified retail 14.02 kernel addresses.
  if(fw == 0x14020000 &&
     (KERNEL_ADDRESS_ALLPROC != KERNEL_ADDRESS_IMAGE_BASE + 0x01B28538 ||
      KERNEL_ADDRESS_PRISON0 != KERNEL_ADDRESS_IMAGE_BASE + 0x0111FA18 ||
      KERNEL_ADDRESS_ROOTVNODE != KERNEL_ADDRESS_IMAGE_BASE + 0x02136E90)) {
    return -ENOSYS;
  }

  KERNEL_ADDRESS_QA_FLAGS = KERNEL_ADDRESS_TARGETID
                                ? KERNEL_ADDRESS_TARGETID + 0x1b
                                : 0;
""",
"14.02 global-address guards")

path.write_text(source)
print(f"Patched {path} with guarded 14.02 kernel initialization")
