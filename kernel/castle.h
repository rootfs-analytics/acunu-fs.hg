#ifndef __CASTLE_H__
#define __CASTLE_H__

#include <asm/byteorder.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/fs.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <net/sock.h>
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,24)
#include <asm/semaphore.h>
#else
#include <linux/semaphore.h>
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)
#define CASTLE_INIT_WORK(_work, _func) INIT_WORK((_work), (void (*)(void *)) (_func), (void *) (_work))
#define CASTLE_DECLARE_WORK(_name, _func) DECLARE_WORK((_name), (void (*)(void *)) _func, &(_name))
#define CASTLE_PREPARE_WORK(_work, _func) PREPARE_WORK((_work), (void (*)(void *)) (_func), (void *) (_work))
#else
#define CASTLE_INIT_WORK(_work, _func) INIT_WORK((_work), (_func))
#define CASTLE_DECLARE_WORK(_name, _func) DECLARE_WORK((_name), (_func))
#define CASTLE_PREPARE_WORK(_work, _func) PREPARE_WORK((_work), (_func))
#endif

#define USED                 __attribute__((used))
#define PACKED               __attribute__((packed))

#define STATIC_BUG_ON_HELPER(expr) \
        (!!sizeof (struct { unsigned int static_assertion_error: (expr) ? -1 : 1; }))
#define STATIC_BUG_ON(expr) \
        extern int (*assert_function__(void)) [STATIC_BUG_ON_HELPER(expr)]

typedef uint32_t tree_seq_t;                   
#define GLOBAL_TREE         ((tree_seq_t)0)
#define INVAL_TREE          ((tree_seq_t)-1)
#define TREE_GLOBAL(_t)     ((_t) == GLOBAL_TREE)
#define TREE_INVAL(_t)      ((_t) == INVAL_TREE)

typedef uint32_t da_id_t;                   
#define INVAL_DA            ((da_id_t)-1)
#define DA_INVAL(_da)       ((_da) == INVAL_DA)

typedef uint32_t block_t;
#define INVAL_BLOCK         ((block_t)-1) 
#define BLOCK_INVAL(_b)     ((_b) == INVAL_BLOCK) 

/* Disk layout related structures */
struct castle_disk_block {
    uint32_t disk;
    block_t  block;
} PACKED;
typedef struct castle_disk_block c_disk_blk_t;
#define INVAL_DISK_BLK          ((c_disk_blk_t){0,0})
#define DISK_BLK_INVAL(_blk)    (((_blk).block == 0) && ((_blk).disk == 0))
#define DISK_BLK_EQUAL(_blk1, _blk2) (((_blk1).disk == (_blk2).disk) && \
                                      ((_blk1).block == (_blk2).block)) 
#define blkfmt                  "(0x%x, 0x%x)"
#define blk2str(_blk)           (_blk).disk, (_blk).block

#define CASTLE_SLAVE_TARGET     (0x00000001)
#define CASTLE_SLAVE_SPINNING   (0x00000002)

#define CASTLE_SLAVE_MAGIC1     (0x02061985)
#define CASTLE_SLAVE_MAGIC2     (0x16071983)
#define CASTLE_SLAVE_MAGIC3     (0x16061981)
struct castle_slave_superblock {
    uint32_t     magic1;
    uint32_t     magic2;
    uint32_t     magic3;
    uint32_t     uuid;
    uint32_t     used;
    uint32_t     size; /* In blocks */
	uint32_t     flags; 
} PACKED;

#define CASTLE_FS_MAGIC1        (0x19731121)
#define CASTLE_FS_MAGIC2        (0x19880624)
#define CASTLE_FS_MAGIC3        (0x19821120)
struct castle_fs_superblock {
    uint32_t     magic1;
    uint32_t     magic2;
    uint32_t     magic3;
    uint32_t     salt;
    uint32_t     peper;
    c_disk_blk_t mstore[16];
} PACKED;

enum {
    CVT_TYPE_TOMB_STONE      = 0x08,
    CVT_TYPE_INLINE          = 0x10,
    CVT_TYPE_ONDISK          = 0x20,
    CVT_TYPE_INVALID         = 0x30,
};

#define MAX_INLINE_VAL_SIZE 512

struct castle_btree_val {
    uint8_t           type;
    uint32_t          length;
    union {
        uint8_t      *val;
        c_disk_blk_t  cdb;
    };
} PACKED;
typedef struct castle_btree_val c_val_tup_t;

#define INVAL_VAL_TUP        ((c_val_tup_t){CVT_TYPE_INVALID, 0, {.cdb = {0, 0}}})

#define CVT_TOMB_STONE(_cvt) ((_cvt).type == CVT_TYPE_TOMB_STONE)
#define CVT_INLINE(_cvt)     ((_cvt).type == CVT_TYPE_INLINE)
#define CVT_ONDISK(_cvt)     ((_cvt).type == CVT_TYPE_ONDISK)
#define CVT_INVALID(_cvt)    ((_cvt).type == CVT_TYPE_INVALID)
#define CVT_ONE_BLK(_cvt)    (CVT_ONDISK(_cvt) &&  (_cvt).length == C_BLK_SIZE)
#define CVT_TOMB_STONE_SET(_cvt)                                            \
                             {                                              \
                                (_cvt).type     = CVT_TYPE_TOMB_STONE;      \
                                (_cvt).length   = 0;                        \
                                (_cvt).cdb.disk = 0;                        \
                                (_cvt).cdb.block= 0;                        \
                             }
#define CVT_INVALID_SET(_cvt)                                               \
                             {                                              \
                                (_cvt).type     = CVT_TYPE_INVALID;         \
                                (_cvt).length   = 0;                        \
                                (_cvt).cdb.disk = 0;                        \
                                (_cvt).cdb.block= 0;                        \
                             }
#define CVT_BTREE_NODE(_cvt, _btree)     \
                            (CVT_ONDISK(_cvt) &&             \
                            (_cvt).length == (_btree)->node_size * C_BLK_SIZE)
#define CVT_INLINE_VAL_LENGTH(_cvt)                                      \
                             (CVT_INLINE(_cvt)?((_cvt).length):0)

#define CVT_EQUAL(_cvt1, _cvt2)                                  \
                             ((_cvt1).type == (_cvt2).type &&         \
                              (_cvt1).length == (_cvt2).length &&     \
                              (!CVT_ONDISK(_cvt1) ||                \
                               DISK_BLK_EQUAL((_cvt1).cdb, (_cvt2).cdb)))

#define CDB_TO_CVT(_cvt, _cdb, _blks)                         \
                                {                                 \
                                    (_cvt).type = CVT_TYPE_ONDISK;    \
                                    (_cvt).length =  _blks * C_BLK_SIZE;  \
                                    (_cvt).cdb = _cdb;          \
                                }


typedef uint8_t c_mstore_id_t;

#define INVAL_MSTORE_KEY             ((c_mstore_key_t){{0,0},0})
#define MSTORE_KEY_INVAL(_k)       (((_k).cdb.disk == 0) && ((_k).cdb.block == 0) && ((_k).idx == 0))
#define MSTORE_KEY_EQUAL(_k1, _k2) (((_k1).cdb.disk  == (_k2).cdb.disk)  && \
                                    ((_k1).cdb.block == (_k2).cdb.block) && \
                                    ((_k1).idx       == (_k2).idx))
typedef struct castle_mstore_key {
    c_disk_blk_t cdb;
    int          idx;
} c_mstore_key_t;

typedef struct castle_mstore {
    c_mstore_id_t              store_id;             /* Id of the store, ptr in fs_sb    */
    size_t                     entry_size;           /* Size of the entries stored       */
    struct semaphore           mutex;                /* Mutex which protects the         */
                                                     /*  last_node_* variables           */
    c_disk_blk_t               last_node_cdb;        /* Tail of the list, has at least   */
                                                     /* one unused entry in it           */
    int                        last_node_unused;     /* Number of unused entries in the  */
                                                     /* last node                        */
} c_mstore_t;

typedef struct castle_mstore_iter {
    struct castle_mstore      *store;                /* Store we are iterating over      */
    struct castle_cache_block *node_c2b;             /* Currently accessed node (locked) */
    int                        node_idx;             /* Next entry index in current node */ 
} c_mstore_iter_t;

enum {
    MSTORE_VERSIONS_ID,
    MSTORE_BLOCK_CNTS,
    MSTORE_DOUBLE_ARRAYS,
    MSTORE_COMPONENT_TREES,
}; 


#define MTREE_TYPE                 0x33
#define MTREE_BVEC_BLOCK(_bvec)   ((sector_t)(_bvec)->key)

#define BATREE_TYPE                0x44

#define VLBA_TREE_TYPE             0x55
                                  
#define MAX_BTREE_DEPTH           (10)
#define MAX_BTREE_ENTRIES         (2500)

typedef uint8_t btree_t;

#define BTREE_NODE_MAGIC  0x0000cdab
struct castle_btree_node {
    uint32_t        magic;
    uint32_t        version;
    uint32_t        used;
    uint8_t         is_leaf;
    /* Payload (i.e. btree entries) depend on the B-tree type */
    btree_t         type;
    c_disk_blk_t    next_node;
    uint8_t         payload[0];
} PACKED;

#define BTREE_NODE_PAYLOAD(_node)   ((void *)&(_node)->payload)

#define PLUS_INFINITY_DIM_LENGTH 0xFFFFFFFF

/* Variable length key, for example used by the btree */
typedef struct castle_var_length_key {
    uint32_t length;
    uint8_t key[0];
} PACKED c_vl_key_t;

typedef struct castle_var_length_object_key {
    uint32_t nr_dims;
    c_vl_key_t *dims[0];
} PACKED c_vl_okey_t;

typedef struct castle_var_length_btree_key {
    uint32_t length;
    uint32_t nr_dims;
    uint32_t dim_head[0];
    /* uint8_t dims[][] */
} PACKED c_vl_bkey_t;

/* Below encapsulates the internal btree node structure, different type of
   nodes may be used for different trees */
struct castle_btree_type {
    btree_t   magic;         /* Also used as an index to castle_btrees
                                array.                                 */
    int       node_size;     /* in C_BLK_SIZE                          */
    void     *min_key;       /* Minimum key                            */
    void     *max_key;       /* Maximum used as the end of node marker */
    void     *inv_key;       /* An invalid key, comparison with it 
                                should always return a negative number
                                except if also compared to invalid key
                                in which case cmp should return zero   */
    int     (*need_split)    (struct castle_btree_node *node,
                              int                       version_or_key);
                             /* 0 - version split, 1 - key split       */
    int     (*key_compare)   (void *key1, void *key2);
                             /* Returns negative if key1 < key2, zero 
                                if equal, positive otherwise           */
    void*   (*key_next)      (void *key);
                             /* Successor key, succ(MAX) = INVAL,
                                succ(INVAL) = INVAL                    */
    void    (*entry_get)     (struct castle_btree_node *node,
                              int                       idx,
                              void                    **key_p,            
                              version_t                *version_p,
                              int                      *is_leaf_ptr_p,
                              c_val_tup_t              *cvt_p);
    void    (*entry_add)     (struct castle_btree_node *node,
                              int                       idx,
                              void                     *key,            
                              version_t                 version,
                              int                       is_leaf_ptr,
                              c_val_tup_t               cvt);
    void    (*entry_replace) (struct castle_btree_node *node,
                              int                       idx,
                              void                     *key,            
                              version_t                 version,
                              int                       is_leaf_ptr,
                              c_val_tup_t               cvt);
    void    (*entries_drop)  (struct castle_btree_node *node,
                              int                       idx_start,
                              int                       idx_end);
                             /* Drop all entries between idx_start and
                                idx_stop. Inclusive                    */ 
    void    (*node_print)    (struct castle_btree_node *node);
#ifdef CASTLE_DEBUG    
    void    (*node_validate) (struct castle_btree_node *node);
#endif        
};

struct castle_component_tree {
    tree_seq_t          seq;
    atomic_t            ref_count;
    atomic_t            write_ref_count;
    atomic64_t          item_count;
    btree_t             btree_type;
    uint8_t             dynamic;           /* 1 - dynamic modlist btree, 0 - merge result */ 
    da_id_t             da;
    uint8_t             level;
    struct rw_semaphore lock;              /* Protects root_node, tree depth & last_node  */
    uint8_t             tree_depth;
    c_disk_blk_t        root_node;
    c_disk_blk_t        first_node;
    c_disk_blk_t        last_node;
    atomic64_t          node_count;
    struct list_head    da_list;
    struct list_head    hash_list;
    c_mstore_key_t      mstore_key;
};
extern struct castle_component_tree castle_global_tree;

struct castle_dlist_entry {
    da_id_t     id;
    version_t   root_version;
} PACKED;

struct castle_clist_entry {
    da_id_t      da_id;
    uint64_t     item_count;
    btree_t      btree_type;
    uint8_t      dynamic;
    tree_seq_t   seq;
    uint8_t      level;
    uint8_t      tree_depth;
    c_disk_blk_t root_node;
    c_disk_blk_t first_node;
    c_disk_blk_t last_node;
    uint64_t     node_count;
} PACKED;

struct castle_vlist_entry {
    uint32_t     version_nr;
    uint32_t     parent;
    da_id_t      da_id;
    uint32_t     size;
} PACKED;

#define MLIST_NODE_MAGIC  0x0000baca
struct castle_mlist_node {
    uint32_t     magic;
    uint16_t     capacity;
    uint16_t     used;
    c_disk_blk_t next;
    uint8_t      payload[0];
} PACKED;

struct castle_flist_entry {
    uint32_t        slave_uuid;
    version_t       version;
    block_t         blocks;
} PACKED;

/* IO related structures */
struct castle_bio_vec;
struct castle_object_replace;

typedef struct castle_bio {
    struct castle_attachment     *attachment;
    /* castle_bio is created to handle a bio, or an rxrpc call (never both) */
    int                           data_dir;
    union {
        struct bio               *bio;
        struct castle_rxrpc_call *rxrpc_call;
        struct castle_object_replace *replace;
    };
    struct castle_bio_vec        *c_bvecs; 
    atomic_t                      count;
    int                           err;
#ifdef CASTLE_DEBUG              
    int                           stuck;
    int                           id;
    int                           nr_bvecs;
    struct list_head              list;
#endif
} c_bio_t;


struct castle_cache_block;
struct castle_request_timeline;
#define CBV_ONE2ONE_BIT               (0) 
#define CBV_ROOT_LOCKED_BIT           (1) 
#define CBV_DOING_SPLITS              (2) 
#define CBV_PARENT_WRITE_LOCKED       (3) 
#define CBV_CHILD_WRITE_LOCKED        (4) 
/* Temporary variable used to set the above correctly, at the right point in time */ 
#define CBV_C2B_WRITE_LOCKED          (5) 

typedef struct castle_bio_vec {
    /* Where did this IO originate from */
    c_bio_t                      *c_bio;
    
    /* What (key, version) do we want to read */
    void                         *key;
    version_t                     version;
    /* Component tree in which to perform the search */
    struct castle_component_tree *tree;
    /* Flags */
    unsigned long                 flags;
    /* Used to walk the B-Tree */
    union {
        struct {
            /* How far down the tree we've gone so far */
            int                        btree_depth;
            /* What's the number of levels in the tree, private copy needed in case
               someone splits the root node while we are lower down in the tree */
            int                        btree_levels;
            /* Key in the parent node under which we found btree_node */
            void                      *parent_key;
            /* When writing, B-Tree node and its parent have to be 
               locked concurrently. */
            struct castle_cache_block *btree_node;
            struct castle_cache_block *btree_parent_node;
        };
    };
    /* Used to thread this bvec onto a workqueue */
    struct work_struct               work;
    /* Value tuple allocation callback */
    void                           (*cvt_get)    (struct castle_bio_vec *, 
                                                  c_val_tup_t,
                                                  c_val_tup_t *);
    /* Completion callback */
    void                           (*endfind)    (struct castle_bio_vec *, int, c_val_tup_t);
    void                           (*da_endfind) (struct castle_bio_vec *, int, c_val_tup_t);
#ifdef CASTLE_DEBUG              
    unsigned long                    state;
    struct castle_cache_block       *locking;
#endif
#ifdef CASTLE_PERF_DEBUG    
    struct castle_request_timeline  *timeline;
#endif
} c_bvec_t;

#define REMOVE                        (2) 

#define c_bvec_data_dir(_c_bvec)      ((_c_bvec)->c_bio->data_dir & RW_MASK)
#define c_bvec_data_del(_c_bvec)      ((_c_bvec)->c_bio->data_dir & REMOVE)
#define c2b_bnode(_c2b)               ((struct castle_btree_node *)c2b_buffer(_c2b))
#define c_bvec_bnode(_c_bvec)           c2b_bnode((_c_bvec)->btree_node)
#define c_bvec_bpnode(_c_bvec)          c2b_buffer((_c_bvec)->btree_parent_node)
#define c_bvec_btree_fn(_c_bvec, _fn) ((_c_bvec)->c_bio->btree->(_fn))

/* Iterface implemented by various iterators in the module. Skip function is optional. */
typedef int  (*castle_iterator_has_next_t)(void *iter);
typedef void (*castle_iterator_next_t)    (void *iter, 
                                           void **key_p, 
                                           version_t *version_p, 
                                           c_val_tup_t *cvt_p);
typedef void (*castle_iterator_skip_t)    (void *iter,
                                           void *key);
typedef void (*castle_iterator_cancel_t)  (void *iter);
struct castle_iterator_type {
    castle_iterator_has_next_t has_next;
    castle_iterator_next_t     next;
    castle_iterator_skip_t     skip;
    castle_iterator_cancel_t   cancel;
};

/* Used to lock nodes pointed to by leaf pointers (refered to as 'indirect nodes') */
struct castle_indirect_node {
    /* Will form array of c2b/{cdb, f_idx} for the indirect nodes. Sorted by
       cdb. May contain holes, if multiple entries in the original node
       point to the same indirect node. Will span at most orig_node->used
       entries. */
    union {
        /* Used before entries are locked */
        struct {
            c_disk_blk_t               cdb;      /* CDB from leaf pointer  */
            uint16_t                   f_idx;    /* Index in the orig node */
        };                             
        /* Used after entries are locked */
        struct {
            struct castle_cache_block *c2b;      /* Cache page for an 'indirect' node */
        };
    };                                 
    /* Will form array indexed by the entry # from the orginal node. Used to find
       the right indirect node/entry in the array above. Again spans at most
       node->used entries. */
    struct {                 
        uint16_t                       r_idx;    /* Index in indirect_nodes array */
        uint16_t                       node_idx; /* Index in the indirect node */ 
    };
}; 

enum {
    C_ITER_ALL_ENTRIES,
    C_ITER_MATCHING_VERSIONS,
    C_ITER_ANCESTRAL_VERSIONS
};

/* Used for iterating through the tree */
typedef struct castle_iterator {
    /* Fields below should be filled in before iterator is registered with the btree 
       code with btree_iter_init() and start() */ 
    int                         (*need_visit)(struct castle_iterator *c_iter, c_disk_blk_t node_cdb);
    void                        (*node_start)(struct castle_iterator *c_iter);
    void                        (*each)      (struct castle_iterator *c_iter, 
                                              int index, 
                                              void *key, 
                                              version_t version,
                                              c_val_tup_t cvt);
    void                        (*node_end)  (struct castle_iterator *c_iter);
    void                        (*end)       (struct castle_iterator *c_iter, int err);
    void                         *private;
    struct castle_component_tree *tree;

    /* Fields below are used by the iterator to conduct the walk */
    int                           type;       /* C_ITER_XXX */
    version_t                     version;
    void                         *parent_key; /* The key we followed to get to the block 
                                                 on the top of the path/stack */
    union {
        /* Only used by C_ITER_ALL_ENTRIES       */
        int                       node_idx[MAX_BTREE_DEPTH];
        /* Only used by C_ITER_MATCHING_VERSIONS */
        void                     *next_key;   /* The next key to look for in the iteration 
                                                 (typically parent_key + 1 when at leafs) */
    };
    int                           cancelled;
    int                           err;
    
    struct castle_cache_block    *path[MAX_BTREE_DEPTH];
    int                           depth;

    struct castle_indirect_node *indirect_nodes; /* If allocated, MAX_BTREE_ENTRIES */
                                
    struct work_struct           work;
} c_iter_t;

/* Enumerates all entries in a modlist btree */
typedef struct castle_enumerator {
    struct castle_component_tree *tree;
    int                           err;
    struct castle_iterator        iterator; 
    wait_queue_head_t             iterator_wq;
    volatile int                  iterator_outs;
    int                           iter_completed;
    /* Variables used to buffer up entries from the iterator */
    int                           prod_idx;
    int                           cons_idx;
    struct castle_btree_node     *buffer;       /* Two buffers are actually allocated (buffer1/2) */
    struct castle_btree_node     *buffer1;      /* buffer points to the one currently used to     */
    struct castle_btree_node     *buffer2;      /* read in a node, second is used to preserve     */
                                                /* key pointer validity. TODO: fix, waseful.      */

    /* Set to decide whether to visit nodes, implemented as hash table */
    struct {
        spinlock_t                visited_lock;
        struct list_head         *visited_hash;
        int                       next_visited;
        int                       max_visited;
        struct castle_visited {
            c_disk_blk_t          cdb;
            struct list_head      list;
        } *visited;
    };
} c_enum_t; 

struct node_buf_t;
struct node_buf_t {
    struct castle_btree_node *node;
    struct list_head          list;
};

/* Enumerates latest version value for all entries */
typedef struct castle_rq_enumerator {
    struct castle_component_tree *tree;
    int                           err;
    version_t                     version;
    struct castle_iterator        iterator; 
    volatile int                  iter_completed;
    wait_queue_head_t             iter_wq;
    volatile int                  iter_running;
    struct node_buf_t            *prod_buf;
    int                           prod_idx;
    struct node_buf_t            *cons_buf;
    int                           cons_idx;
    struct node_buf_t            *buf_head;
    int                           buf_count;
    void                         *cur_key;
    void                         *start_key;
    void                         *end_key;
    int                           in_range;
} c_rq_enum_t;

typedef struct castle_merged_iterator {
    int nr_iters;
    struct castle_btree_type *btree;
    int err;
    int non_empty_cnt;
    struct component_iterator {
        int                          completed;
        void                        *iterator;
        struct castle_iterator_type *iterator_type;
        int                          cached;
        struct {           
            void                    *k;
            version_t                v;
            c_val_tup_t              cvt;
        } cached_entry;
    } *iterators;
} c_merged_iter_t;

typedef struct castle_da_rq_iterator {
    int                       nr_cts;
    int                       err;
    struct castle_btree_type *btree;
    c_merged_iter_t           merged_iter;

    struct ct_rq {
        struct castle_component_tree *ct;
        c_rq_enum_t                   ct_rq_iter; 
    } *ct_rqs;
} c_da_rq_iter_t;


#define BLOCKS_HASH_SIZE        (100)
struct castle_slave_block_cnt
{
    version_t        version;
    block_t          cnt;
    struct list_head list;
    c_mstore_key_t   mstore_key;
};

struct castle_slave_block_cnts 
{
    struct list_head hash[BLOCKS_HASH_SIZE];     /* The hashtable is protected by 
                                                    castle_slave superblock lock   */
    struct castle_slave_block_cnt metadata_cnt;  /* Count for version 0 (metadata) */
};

/* First class structures */
struct castle {
    struct kobject kobj;
};

struct castle_slave {
    uint32_t                        id;
    uint32_t                        uuid; /* Copy of the uuid from the superblock
                                             needed here, because we cannot cache
                                             the superblock without being able to
                                             _find_by_uuid */
    int                             new_dev;
    struct kobject                  kobj;
    struct list_head                list;
    struct block_device            *bdev;
    struct castle_cache_block      *sblk;
    struct castle_cache_block      *fs_sblk;
    block_t                         free_blk;
    struct castle_slave_block_cnts  block_cnts;
    unsigned long                   last_access;
};

struct castle_slaves {
    struct kobject   kobj;
    struct list_head slaves;
};

/* Castle attachment represents a block device or an attached object collection */
struct castle_attachment {
    version_t           version;
    int                 users;
    struct rw_semaphore lock;
    int                 device; /* !=0 if block device, == 0 if object collection */
    union {
        struct {
            struct gendisk  *gd;
        } dev; /* Only valid for block devices */ 
        struct {
            collection_id_t  id;
            char            *name;
        } col; /* Only valid for object collections */
    };

    struct kobject      kobj;
    int                 sysfs_registered;
    struct list_head    list;
};

struct castle_attachments { 
    struct kobject collections_kobj;
    struct kobject devices_kobj;
    int major;
    struct list_head attachments;
};

struct castle_transfer {
    transfer_id_t           id;
    version_t               version;
    int                     direction;
    atomic_t                progress;
    int                     finished;
    int                     error;

/*    struct castle_region  **regions;
    int                     regions_count; */
    
    struct kobject          kobj;
    struct list_head        list;
    
    c_iter_t                c_iter;
    atomic_t                phase;
    struct completion       completion;
};

struct castle_transfers {
    struct kobject   kobj;
    struct list_head transfers;
};

extern struct castle              castle;
extern struct castle_slaves       castle_slaves;
extern struct castle_attachments  castle_attachments;
extern struct castle_transfers    castle_transfers;
extern da_id_t                    castle_next_da_id;

extern struct workqueue_struct *castle_wqs[2*MAX_BTREE_DEPTH+1];
#define castle_wq              (castle_wqs[0])

/* Various utilities */
#define C_BLK_SHIFT                    (12) 
#define C_BLK_SIZE                     (1 << C_BLK_SHIFT)
#define disk_blk_to_offset(_cdb)     ((_cdb).block * C_BLK_SIZE)

struct castle_attachment*                          
                      castle_device_init           (version_t version);
void                  castle_device_free           (struct castle_attachment *cd);
struct castle_attachment*                          
                      castle_device_find           (dev_t dev);
                                                   
struct castle_attachment* 
                      castle_collection_init       (version_t version, char *name);
void                  castle_collection_free       (struct castle_attachment *ca);
struct castle_attachment* 
                      castle_collection_find       (collection_id_t col_id);

struct castle_slave*  castle_claim                 (uint32_t new_dev);
void                  castle_release               (struct castle_slave *cs);
                                                   
void                  castle_slave_access          (uint32_t uuid);
                                                   
struct castle_slave*  castle_slave_find_by_id      (uint32_t id);
struct castle_slave*  castle_slave_find_by_uuid    (uint32_t uuid);
struct castle_slave*  castle_slave_find_by_block   (c_disk_blk_t cdb);

struct castle_slave_superblock* 
                      castle_slave_superblock_get  (struct castle_slave *cs);
void                  castle_slave_superblock_put  (struct castle_slave *cs, int dirty);
struct castle_fs_superblock* 
                      castle_fs_superblocks_get    (void);
void                  castle_fs_superblocks_put    (struct castle_fs_superblock *sb, int dirty);

int                   castle_fs_init               (void);

struct castle_cache_block;

struct castle_object_replace {
    uint32_t    value_len; // total value length

    void        (*complete)        (struct castle_object_replace *op,
                                    int                           err);
    void        (*replace_continue)(struct castle_object_replace *op);
    uint32_t    (*data_length_get) (struct castle_object_replace *op);
    void        (*data_copy)       (struct castle_object_replace *op, 
                                    void                         *buffer, 
                                    int                           str_length, 
                                    int                           partial);

    struct castle_cache_block *data_c2b;
    uint32_t    data_c2b_offset;
    uint32_t    data_length;
};

#endif /* __CASTLE_H__ */
