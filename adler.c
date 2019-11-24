#include <minix/drivers.h>
#include <minix/chardriver.h>
#include <stdio.h>
#include <stdlib.h>
#include <minix/ds.h>

#define ADLER_MOD 65521

/*
 * Function prototypes for the adler driver.
 */
static int adler_open(devminor_t minor, int access, endpoint_t user_endpt);
static int adler_close(devminor_t minor);
static ssize_t adler_read(devminor_t minor, u64_t position, endpoint_t endpt,
    cp_grant_id_t grant, size_t size, int flags, cdev_id_t id);
static ssize_t adler_write(devminor_t minor, u64_t position, endpoint_t endpt,
    cp_grant_id_t grant, size_t size, int flags, cdev_id_t id);

/* SEF functions and variables. */
static void sef_local_startup(void);
static int sef_cb_init(int type, sef_init_info_t *info);
static int sef_cb_lu_state_save(int);
static int lu_state_restore(void);

/* Entry points to the adler driver. */
static struct chardriver adler_tab =
{
    .cdr_open	= adler_open,
    .cdr_close	= adler_close,
    .cdr_read	= adler_read,
    .cdr_write  = adler_write,
};

static u32_t a;
static u32_t b;

static int adler_open(devminor_t UNUSED(minor), int UNUSED(access),
    endpoint_t UNUSED(user_endpt))
{
    return OK;
}

static int adler_close(devminor_t UNUSED(minor))
{
    return OK;
}

static ssize_t adler_read(devminor_t UNUSED(minor), u64_t position,
    endpoint_t endpt, cp_grant_id_t grant, size_t size, int UNUSED(flags),
    cdev_id_t UNUSED(id))
{
    u64_t dev_size;
    char *ptr;
    int ret;
    printf("%llu, %d\n", position, size);
    if (size < 8) {
        return EINVAL;
    }
    if (position >= 8) {
        return 0;
    }

    char hex_a[10];
    char hex_b[10];

    sprintf(hex_a, "%04x", a);
    sprintf(hex_b, "%04x", b);

    strcat(hex_b, hex_a);
    char* hex = hex_b;

    ptr = hex_b;
    if ((ret = sys_safecopyto(endpt, grant, 0, (vir_bytes) ptr, 8)) != OK)
        return ret;

    a = 1;
    b = 0;

    /* Return the number of bytes read. */
    return 8;
}

static ssize_t adler_write(devminor_t UNUSED(minor), u64_t position,
    endpoint_t endpt, cp_grant_id_t grant, size_t size, int UNUSED(flags),
    cdev_id_t UNUSED(id))
{
    char *ptr;
    int ret;
    int bufsize = 100;
    char buf[bufsize];

    int i = 0;
    int read_len;
    ptr = buf;
    while (i < size) {
        int j = 0;
        if (size - i < bufsize) {
            read_len = size - i;
        } else {
            read_len = bufsize;
        }
        if ((ret = sys_safecopyfrom(endpt, grant, i, (vir_bytes) ptr, read_len)) != OK)
            return ret;
        while (j < read_len && i < size) {
            a += ptr[j];
            a = a % ADLER_MOD;
            b += a;
            b = b % ADLER_MOD;
            ++j;
            ++i;
        }
    }
    
    /* Return the number of bytes read. */
    return size;
}

static int sef_cb_lu_state_save(int UNUSED(state)) {
/* Save the state. */
    ds_publish_u32("a", a, DSF_OVERWRITE);
    ds_publish_u32("b", b, DSF_OVERWRITE);

    return OK;
}

static int lu_state_restore() {
/* Restore the state. */
    u32_t value_a;
    u32_t value_b;

    ds_retrieve_u32("a", &value_a);
    ds_retrieve_u32("b", &value_b);
    ds_delete_u32("a");
    ds_delete_u32("b");
    a = value_a;
    b = value_b;

    return OK;
}

static void sef_local_startup()
{
    /*
     * Register init callbacks. Use the same function for all event types
     */
    sef_setcb_init_fresh(sef_cb_init);
    sef_setcb_init_lu(sef_cb_init);
    sef_setcb_init_restart(sef_cb_init);

    /*
     * Register live update callbacks.
     */
    /* - Agree to update immediately when LU is requested in a valid state. */
    sef_setcb_lu_prepare(sef_cb_lu_prepare_always_ready);
    /* - Support live update starting from any standard state. */
    sef_setcb_lu_state_isvalid(sef_cb_lu_state_isvalid_standard);
    /* - Register a custom routine to save the state. */
    sef_setcb_lu_state_save(sef_cb_lu_state_save);

    /* Let SEF perform startup. */
    sef_startup();
}

static int sef_cb_init(int type, sef_init_info_t *UNUSED(info))
{
/* Initialize the adler driver. */

    a = 1;
    b = 0;
    switch(type) {
        case SEF_INIT_FRESH:
        break;
        case SEF_INIT_LU:
            /* Restore the state. */
            lu_state_restore();
        break;
        case SEF_INIT_RESTART:
        break;
    }

    /* Initialization completed successfully. */
    return OK;
}

int main(void)
{
    /*
     * Perform initialization.
     */
    sef_local_startup();

    /*
     * Run the main loop.
     */
    chardriver_task(&adler_tab);
    return OK;
}

