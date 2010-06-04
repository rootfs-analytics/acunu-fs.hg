#ifndef __CASTLE_FREESPACE_H__
#define __CASTLE_FREESPACE_H__

void         castle_freespace_slave_init               (struct castle_slave *cs, int fresh);
c_disk_blk_t castle_freespace_slave_block_get          (struct castle_slave *cs, version_t version, int size);
c_disk_blk_t castle_freespace_block_get                (version_t version, int size);
void         castle_freespace_block_free               (c_disk_blk_t cdb, version_t version, int size);
                                                       
int          castle_freespace_version_add              (version_t version);
ssize_t      castle_freespace_summary_get              (struct castle_slave *cs, char *buf, int version_offset, int number);
ssize_t      castle_freespace_version_slave_blocks_get (struct castle_slave *cs, version_t version);
ssize_t      castle_freespace_version_blocks_get       (version_t version);

int          castle_freespace_init                     (void);
void         castle_freespace_fini                     (void);

#endif /* __CASTLE_FREESPACE_H__ */