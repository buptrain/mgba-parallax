#include <assert.h>
#include <fcntl.h>

#include "gba_xr_state_file_policy.h"

int main(void) {
  assert((GBAXR_STATE_SAVE_OPEN_FLAGS & (O_WRONLY | O_RDWR)) == O_RDWR);
  assert((GBAXR_STATE_SAVE_OPEN_FLAGS & O_TRUNC) != 0);
  assert((GBAXR_STATE_SAVE_OPEN_FLAGS & O_CREAT) != 0);
  return 0;
}
