#ifndef GBA_XR_STATE_FILE_POLICY_H
#define GBA_XR_STATE_FILE_POLICY_H

#include <fcntl.h>

/* mCoreSaveStateNamed serializes through VFile::map with both read and write
 * protection. Android's VFile FD backend therefore needs a read/write file
 * descriptor even though the adapter only produces state bytes. */
#define GBAXR_STATE_SAVE_OPEN_FLAGS (O_RDWR | O_TRUNC | O_CREAT)

#endif
