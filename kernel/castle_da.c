#include <linux/sched.h>

#include "castle_public.h"
#include "castle_utils.h"
#include "castle.h"
#include "castle_cache.h"
#include "castle_btree.h"
#include "castle_versions.h"
#include "castle_freespace.h"

//#define DEBUG
#ifndef DEBUG
#define debug(_f, ...)  ((void)0)
#else
#define debug(_f, _a...)  (printk("%s:%.4d: " _f, __FILE__, __LINE__ , ##_a))
#endif

#define MAX_DA_LEVEL                    (10)

#define CASTLE_DA_HASH_SIZE             (1000)
static struct list_head     *castle_da_hash;
static struct castle_mstore *castle_da_store;
static struct castle_mstore *castle_tree_store;
       da_id_t               castle_next_da_id    = 1; 
static tree_seq_t            castle_next_tree_seq = 1; 

struct castle_double_array {
    da_id_t          id;
    version_t        root_version;
    struct list_head trees[MAX_DA_LEVEL];
    struct list_head hash_list;
    c_mstore_key_t   mstore_key;
};

struct castle_component_tree {
    tree_seq_t       seq;
    uint8_t          level;
    c_disk_blk_t     first_node;
    struct list_head list;
    c_mstore_key_t   mstore_key;
};

DEFINE_HASH_TBL(castle_da, castle_da_hash, CASTLE_DA_HASH_SIZE, struct castle_double_array, hash_list, da_id_t, id);

static int castle_da_ct_inc_cmp(struct list_head *l1, struct list_head *l2)
{
    struct castle_component_tree *ct1 = list_entry(l1, struct castle_component_tree, list);
    struct castle_component_tree *ct2 = list_entry(l2, struct castle_component_tree, list);

    return ct1->seq > ct2->seq ? 1 : -1;
}

static c_mstore_key_t castle_da_marshall(struct castle_dlist_entry *dam,
                                         struct castle_double_array *da)
{
    dam->id           = da->id;
    dam->root_version = da->root_version;

    return da->mstore_key;
}
 
static void castle_da_unmarshall(struct castle_double_array *da,
                                 struct castle_dlist_entry *dam,
                                 c_mstore_key_t key)
{
    int i;

    da->id           = dam->id;
    da->root_version = dam->root_version;
    da->mstore_key   = key;

    for(i=0; i<MAX_DA_LEVEL; i++)
        INIT_LIST_HEAD(&da->trees[i]);
}

static int castle_da_trees_sort(struct castle_double_array *da, void *unused)
{
    int i;

    for(i=0; i<MAX_DA_LEVEL; i++)
        list_sort(&da->trees[i], castle_da_ct_inc_cmp);

    return 0;
}

static c_mstore_key_t castle_da_ct_marshall(struct castle_clist_entry *ctm,
                                            struct castle_double_array *da,
                                            struct castle_component_tree *ct)
{
    ctm->da_id      = da->id; 
    ctm->seq        = ct->seq;
    ctm->level      = ct->level;
    ctm->first_node = ct->first_node;

    return ct->mstore_key;
}

static da_id_t castle_da_ct_unmarshall(struct castle_component_tree *ct,
                                       struct castle_clist_entry *ctm,
                                       c_mstore_key_t key)
{
    ct->seq        = ctm->seq;
    ct->level      = ctm->level;
    ct->first_node = ctm->first_node;
    ct->mstore_key = key;
    INIT_LIST_HEAD(&ct->list);

    return ctm->da_id;
}

static void castle_da_foreach_tree(struct castle_double_array *da,
                                   int (*fn)(struct castle_double_array *da,
                                             struct castle_component_tree *ct,
                                             int level_cnt,
                                             void *token), 
                                   void *token)
{
    struct castle_component_tree *ct;
    struct list_head *lh, *t;
    int i, j;

    for(i=0; i<MAX_DA_LEVEL; i++)
    {
        j = 0;
        list_for_each_safe(lh, t, &da->trees[i])
        {
            ct = list_entry(lh, struct castle_component_tree, list); 
            if(fn(da, ct, j, token))
                return;
            j++;
        }
    }
}

static int castle_da_ct_dealloc(struct castle_double_array *da,
                                struct castle_component_tree *ct,
                                int level_cnt,
                                void *unused)
{
    list_del(&ct->list);
    kfree(ct);

    return 0;
}

static int castle_da_hash_dealloc(struct castle_double_array *da, void *unused) 
{
    castle_da_foreach_tree(da, castle_da_ct_dealloc, NULL);
    list_del(&da->hash_list);
    kfree(da);

    return 0;
}

static void castle_da_hash_destroy(void)
{
   castle_da_hash_iterate(castle_da_hash_dealloc, NULL); 
   kfree(castle_da_hash);
}

static int castle_da_tree_writeback(struct castle_double_array *da,
                                    struct castle_component_tree *ct,
                                    int level_cnt,
                                    void *unused)
{
    struct castle_clist_entry mstore_entry;
    c_mstore_key_t key;

    key = castle_da_ct_marshall(&mstore_entry, da, ct); 
    if(MSTORE_KEY_INVAL(key))
    {
        debug("Inserting CT seq=%d\n", ct->seq);
        ct->mstore_key = 
            castle_mstore_entry_insert(castle_tree_store, &mstore_entry);
    }
    else
    {
        debug("Updating CT seq=%d\n", ct->seq);
        castle_mstore_entry_update(castle_tree_store, key, &mstore_entry);
    }

    return 0;
}

static int castle_da_writeback(struct castle_double_array *da, void *unused) 
{
    struct castle_dlist_entry mstore_dentry;
    c_mstore_key_t key;

    key = castle_da_marshall(&mstore_dentry, da);

    /* We get here with hash spinlock held. But since we're calling sleeping functions
       we need to drop it. Hash consitancy is guaranteed, because by this point 
       noone should be modifying it anymore */
    spin_unlock_irq(&castle_da_hash_lock);
    castle_da_foreach_tree(da, castle_da_tree_writeback, NULL);
    if(MSTORE_KEY_INVAL(key))
    {
        debug("Inserting a DA id=%d\n", da->id);
        da->mstore_key = 
            castle_mstore_entry_insert(castle_da_store, &mstore_dentry);
    }
    else
    {
        debug("Updating a DA id=%d.\n", da->id);
        castle_mstore_entry_update(castle_da_store, key, &mstore_dentry);
    }
    spin_lock_irq(&castle_da_hash_lock);

    return 0;
}

static void castle_da_hash_writeback(void)
{
   castle_da_hash_iterate(castle_da_writeback, NULL); 
}
    
int castle_double_array_read(void)
{
    struct castle_dlist_entry mstore_dentry;
    struct castle_clist_entry mstore_centry;
    struct castle_mstore_iter *iterator;
    struct castle_component_tree *ct;
    struct castle_double_array *da;
    c_mstore_key_t key;
    da_id_t da_id;

    castle_da_store   = castle_mstore_open(MSTORE_DOUBLE_ARRAYS,
                                         sizeof(struct castle_dlist_entry));
    castle_tree_store = castle_mstore_open(MSTORE_COMPONENT_TREES,
                                         sizeof(struct castle_clist_entry));

    if(!castle_da_store || !castle_tree_store)
        return -ENOMEM;
    
    /* Read doubling arrays */
    iterator = castle_mstore_iterate(castle_da_store);
    if(!iterator)
        return -EINVAL;
    while(castle_mstore_iterator_has_next(iterator))
    {
        castle_mstore_iterator_next(iterator, &mstore_dentry, &key);
        da = kmalloc(sizeof(struct castle_double_array), GFP_KERNEL);
        if(!da) 
            goto out_iter_destroy;
        castle_da_unmarshall(da, &mstore_dentry, key);
        castle_da_hash_add(da);
        debug("Read DA id=%d\n", da->id);
        castle_next_da_id = (da->id >= castle_next_da_id) ? da->id + 1 : castle_next_da_id;
    }
    castle_mstore_iterator_destroy(iterator);

    /* Read component trees */
    iterator = castle_mstore_iterate(castle_tree_store);
    if(!iterator)
        return -EINVAL;
   
    while(castle_mstore_iterator_has_next(iterator))
    {
        castle_mstore_iterator_next(iterator, &mstore_centry, &key);
        ct = kmalloc(sizeof(struct castle_component_tree), GFP_KERNEL);
        if(!ct)
            goto out_iter_destroy;
        da_id = castle_da_ct_unmarshall(ct, &mstore_centry, key);
        da = castle_da_hash_get(da_id);
        if(!da)
            goto out_iter_destroy;
        debug("Read CT seq=%d\n", ct->seq);
        list_add(&ct->list, &da->trees[ct->level]);
        castle_next_tree_seq = (ct->seq >= castle_next_tree_seq) ? ct->seq + 1 : castle_next_tree_seq;
    }
    castle_mstore_iterator_destroy(iterator);
    debug("castle_next_da_id = %d, castle_next_tree_id=%d\n", 
            castle_next_da_id, 
            castle_next_tree_seq);

    /* Sort all the tree lists by the sequence number */
    castle_da_hash_iterate(castle_da_trees_sort, NULL); 

    return 0;

out_iter_destroy:
    castle_mstore_iterator_destroy(iterator);
    return -EINVAL;
}

static int castle_da_ct_make(struct castle_double_array *da)
{
    struct castle_component_tree *ct;
    c2_block_t *c2b;
    int ret;

    ct = kzalloc(sizeof(struct castle_double_array), GFP_KERNEL); 
    if(!ct) 
        return -ENOMEM;
    
    /* TODO: work out locking for ALL of this! */

    /* Allocate an id for the tree, init the ct. */
    ct->seq         = castle_next_tree_seq++;
    ct->level      = 0;
    ct->mstore_key = INVAL_MSTORE_KEY; 

    /* Create a root node for this tree, and update the root version */
    c2b = castle_btree_node_create(da->root_version, 1 /* is_leaf */, MTREE_TYPE);
    ct->first_node = c2b->cdb; 
    unlock_c2b(c2b);
    put_c2b(c2b);
    castle_version_lock(da->root_version);
    ret = castle_version_root_update(da->root_version, ct->seq, ct->first_node);
    castle_version_unlock(da->root_version);
    if(ret)
    {
        /* TODO: free the block */
        printk("Could not write root node for version: %d\n", da->root_version);
        kfree(ct);
        return ret;
    }
    /* Thread CT onto level 0 list */
    list_add(&ct->list, &da->trees[ct->level]);

    return 0;
}

int castle_double_array_make(da_id_t da_id, version_t root_version)
{
    struct castle_double_array *da;
    int ret, i;

    printk("Creating doubling array for da_id=%d, version=%d\n", da_id, root_version);
    da = kzalloc(sizeof(struct castle_double_array), GFP_KERNEL); 
    if(!da)
        return -ENOMEM;
    da->id = da_id;
    da->root_version = root_version;
    da->mstore_key = INVAL_MSTORE_KEY;
    for(i=0; i<MAX_DA_LEVEL; i++)
        INIT_LIST_HEAD(&da->trees[i]);
    ret = castle_da_ct_make(da);
    if(ret)
    {
        printk("Exiting from failed ct create.\n");
        kfree(da);
        
        return ret;
    }
    printk("Successfully made a new doubling array, id=%d, for version=%d\n",
        da_id, root_version);
    castle_da_hash_add(da);

    return 0;
}

static void castle_da_bvec_complete(c_bvec_t *c_bvec, int err, c_disk_blk_t cdb)
{
    void (*callback) (struct castle_bio_vec *c_bvec, int err, c_disk_blk_t cdb) =
        c_bvec->da_endfind;
    
    callback(c_bvec, err, cdb);
}

void castle_double_array_find(c_bvec_t *c_bvec)
{
    BUG_ON(c_bvec->da_endfind);
    c_bvec->da_endfind = c_bvec->endfind;
    c_bvec->endfind = castle_da_bvec_complete;
    
    castle_btree_find(c_bvec);
}

int castle_double_array_create(void)
{
    castle_da_store   = castle_mstore_init(MSTORE_DOUBLE_ARRAYS,
                                         sizeof(struct castle_double_array));
    castle_tree_store = castle_mstore_init(MSTORE_COMPONENT_TREES,
                                         sizeof(struct castle_component_tree));

    if(!castle_da_store || !castle_tree_store)
        return -ENOMEM;

    return 0;
}
    
int castle_double_array_init(void)
{
    printk("\n========= Double Array init ==========\n");
    castle_da_hash = castle_da_hash_alloc();
    if(!castle_da_hash)
        return -ENOMEM;
    castle_da_hash_init();

    return 0;
}

void castle_double_array_fini(void)
{
    printk("\n========= Double Array fini ==========\n");
    castle_da_hash_writeback();
    castle_da_hash_destroy();
}