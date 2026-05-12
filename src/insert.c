#include "sar.h"

/* ----------------------------------------------------------------------------
 * insert
 *
 * Append to an archive at 'archive_path' all files in 
 * 'filepaths[0..count-1]'.
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
int insert(FILE *archive, const char **filepaths, int count, int verbose){
  /* Local variables */
  int   result = 0;
  int   it = 0;

  /* Code */
  for (it = 0; it < count; ++it){
    if(pack_file(archive, filepaths[it], verbose) != 0){
      result = -1; /* record failure but keep packing */
    }
  }

  return result;
}

