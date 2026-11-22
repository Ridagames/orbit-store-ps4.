Four upstream source/toolchain archives exceed GitHub's 100 MB single-file limit.
Their original bytes are preserved as consecutive .part000, .part001, ... chunks.
To restore the original archive files, run:
  bash REASSEMBLE-LARGE-FILES.sh
Run this script from any directory; it writes the reconstructed files beside their chunks.
