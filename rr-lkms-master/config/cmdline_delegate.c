#include "cmdline_delegate.h"
#include "../common.h" //commonly used headers in this module
#include "../internal/call_protected.h" //used to call cmdline_proc_show()
#include <linux/seq_file.h> //struct seq_file

#define ensure_cmdline_param(cmdline_param) \
    if (strncmp(param_pointer, cmdline_param, strlen_static(cmdline_param)) != 0) { return false; }

#define ensure_cmdline_token(cmdline_param) \
    if (strncmp(param_pointer, cmdline_param, sizeof(cmdline_param)) != 0) { return false; }

/**
 * Extracts device model (syno_hw_version=<string>) from kernel cmd line
 *
 * @param model pointer to save model to
 * @param param_pointer currently processed token
 * @return true on match, false if param didn't match
 */
static bool extract_hw(syno_hw *model, const char *param_pointer)
{
    ensure_cmdline_param(CMDLINE_KT_HW);

    if (strscpy((char *)model, param_pointer + strlen_static(CMDLINE_KT_HW), sizeof(syno_hw)) < 0)
        pr_loc_wrn("HW version truncated to %zu", sizeof(syno_hw)-1);

    pr_loc_dbg("HW version set to: %s", (char *)model);

    return true;
}

/**
 * Extracts serial number (sn=<string>) from kernel cmd line
 *
 * @param sn pointer to save s/n to
 * @param param_pointer currently processed token
 * @return true on match, false if param didn't match
 */
static bool extract_sn(serial_no *sn, const char *param_pointer)
{
    ensure_cmdline_param(CMDLINE_KT_SN);

    if(strscpy((char *)sn, param_pointer + strlen_static(CMDLINE_KT_SN), sizeof(serial_no)) < 0)
        pr_loc_wrn("S/N truncated to %zu", sizeof(serial_no)-1);

    pr_loc_dbg("S/N set to: %s", (char *)sn);

    return true;
}

static bool extract_boot_media_type(struct boot_media *boot_media, const char *param_pointer)
{
    ensure_cmdline_param(CMDLINE_KT_SATADOM);

    char value = param_pointer[strlen_static(CMDLINE_KT_SATADOM)];

    switch (value) {
        case CMDLINE_KT_SATADOM_NATIVE:
            boot_media->type = BOOT_MEDIA_SATA_DOM;
            pr_loc_dbg("Boot media SATADOM (native) requested");
            break;

        case CMDLINE_KT_SATADOM_FAKE:
            boot_media->type = BOOT_MEDIA_SATA_DISK;
            pr_loc_dbg("Boot media SATADISK (fake) requested");
            break;

        case CMDLINE_KT_SATADOM_DISABLED:
            //There's no point to set that option but it's not an error
            pr_loc_wrn("SATA-based boot media disabled (default will be used, %s0 is a noop)", CMDLINE_KT_SATADOM);
            break;

        default:
            pr_loc_err("Option \"%s%c\" is invalid (value should be 0/1/2)", CMDLINE_KT_SATADOM, value);
    }

    return true;
}

/**
 * Extracts VID override (vid=<uint>) from kernel cmd line
 *
 * @param user_vid pointer to save VID
 * @param param_pointer currently processed token
 * @return true on match, false if param didn't match
 */
static bool extract_vid(device_id *user_vid, const char *param_pointer)
{
    ensure_cmdline_param(CMDLINE_CT_VID);

    long long numeric_param;
    int tmp_call_res = kstrtoll(param_pointer + strlen_static(CMDLINE_CT_VID), 0, &numeric_param);
    if (unlikely(tmp_call_res != 0)) {
        pr_loc_err("Call to %s() failed => %d", "kstrtoll", tmp_call_res);
        return true;
    }

    if (unlikely(numeric_param > VID_PID_MAX)) {
        pr_loc_err("Cmdline %s is invalid (value larger than %d)", CMDLINE_CT_VID, VID_PID_MAX);
        return true;
    }

    if (unlikely(*user_vid) != 0)
        pr_loc_wrn(
                "VID was already set to 0x%04x by a previous instance of %s - it will be changed now to 0x%04x",
                *user_vid, CMDLINE_CT_VID, (unsigned int)numeric_param);

    *user_vid = (unsigned int)numeric_param;
    pr_loc_dbg("VID override: 0x%04x", *user_vid);

    return true;
}

/**
 * Extracts PID override (pid=<uint>) from kernel cmd line
 *
 * @param user_pid pointer to save PID
 * @param param_pointer currently processed token
 * @return true on match, false if param didn't match
 */
static bool extract_pid(device_id *user_pid, const char *param_pointer)
{
    ensure_cmdline_param(CMDLINE_CT_PID);

    long long numeric_param;
    int tmp_call_res = kstrtoll(param_pointer + strlen_static(CMDLINE_CT_PID), 0, &numeric_param);
    if (unlikely(tmp_call_res != 0)) {
        pr_loc_err("Call to %s() failed => %d", "kstrtoll", tmp_call_res);
        return true;
    }

    if (unlikely(numeric_param > VID_PID_MAX)) {
        pr_loc_err("Cmdline %s is invalid (value larger than %d)", CMDLINE_CT_PID, VID_PID_MAX);
        return true;
    }

    if (unlikely(*user_pid) != 0)
        pr_loc_wrn(
                "PID was already set to 0x%04x by a previous instance of %s - it will be changed now to 0x%04x",
                *user_pid, CMDLINE_CT_PID, (unsigned int)numeric_param);

    *user_pid = (unsigned int)numeric_param;
    pr_loc_dbg("PID override: 0x%04x", *user_pid);

    return true;
}

/**
 * Extracts MFG mode enable switch (mfg<noval>) from kernel cmd line
 *
 * @param is_mfg_boot pointer to flag
 * @param param_pointer currently processed token
 * @return true on match, false if param didn't match
 */
static bool extract_mfg(bool *is_mfg_boot, const char *param_pointer)
{
    ensure_cmdline_token(CMDLINE_CT_MFG);

    *is_mfg_boot = true;
    pr_loc_dbg("MFG boot requested");

    return true;
}

/**
 * Extracts maximum size of SATA DOM (dom_szmax=<number of MiB>) from kernel cmd line
 */
static bool extract_dom_max_size(struct boot_media *boot_media, const char *param_pointer)
{
    ensure_cmdline_param(CMDLINE_CT_DOM_SZMAX);

    long size_mib = simple_strtol(param_pointer + strlen_static(CMDLINE_CT_DOM_SZMAX), NULL, 10);
    if (size_mib <= 0) {
        pr_loc_err("Invalid maximum size of SATA DoM (\"%s=%ld\")", CMDLINE_CT_DOM_SZMAX, size_mib);
        return true;
    }

    boot_media->dom_size_mib = size_mib;
    pr_loc_dbg("Set maximum SATA DoM to %ld", size_mib);

    return true;
}

/**
 * Extracts MFG mode enable switch (syno_port_thaw=<1|0>) from kernel cmd line
 *
 * @param port_thaw pointer to flag
 * @param param_pointer currently processed token
 * @return true on match, false if param didn't match
 */
static bool extract_port_thaw(bool *port_thaw, const char *param_pointer)
{
    ensure_cmdline_param(CMDLINE_KT_THAW);

    short value = param_pointer[strlen_static(CMDLINE_KT_THAW)];

    if (value == '0') {
        *port_thaw = false;
        goto out_found;
    }
    if (value == '1') {
        *port_thaw = true;
        goto out_found;
    }

    if (value == '\0') {
        pr_loc_err("Option \"%s%d\" is invalid (value should be 0 or 1)", CMDLINE_KT_THAW, value);
        return true;
    }

    out_found:
        pr_loc_dbg("Port thaw set to: %d", port_thaw?1:0);
        return true;
}

/**
 * Extracts number of expected network interfaces (netif_num=<number>) from kernel cmd line
 *
 * @param netif_num pointer to save number
 * @param param_pointer currently processed token
 * @return true on match, false if param didn't match
 */
static bool extract_netif_num(unsigned short *netif_num, const char *param_pointer)
{
    ensure_cmdline_param(CMDLINE_KT_NETIF_NUM);

    short value = *(param_pointer + strlen_static(CMDLINE_KT_NETIF_NUM)) - 48; //ASCII: 0=48 and 9=57

    if (value == 0) {
        pr_loc_wrn("You specified no network interfaces (\"%s=0\")", CMDLINE_KT_NETIF_NUM);
        return true;
    }

    if (value < 1 || value > 9) {
        pr_loc_err("Invalid number of network interfaces set (\"%s%d\")", CMDLINE_KT_NETIF_NUM, value);
        return true;
    }

    *netif_num = value;
    pr_loc_dbg("Declared network ifaces # as %d", value);

    return true;
}

/**
 * Extracts network interfaces MAC addresses (mac1...mac4=<MAC> **OR** macs=<mac1,mac2,macN>)
 *
 * Note: mixing two notations may lead to undefined behaviors
 *
 * @param macs pointer to save macs
 * @param param_pointer currently processed token
 * @return true on match, false if param didn't match
 */
static bool extract_netif_macs(mac_address *macs[MAX_NET_IFACES], const char *param_pointer)
{
    if (strncmp(param_pointer, CMDLINE_KT_MACS, strlen_static(CMDLINE_KT_MACS)) == 0) {
        unsigned short i = 0;
        const char *pBegin = param_pointer + strlen_static(CMDLINE_KT_MACS);
        char *pEnd = strchr(pBegin, ',');

        while (NULL != pEnd && MAX_NET_IFACES > i) {
            *pEnd = '\0';
            macs[i] = kmalloc(sizeof(mac_address), GFP_KERNEL);
            if (unlikely(!macs[i])) {
                pr_loc_crt("kernel memory alloc failure - tried to allocate %lu bytes for macs[%d]", sizeof(mac_address),
                           i);
                goto out_found;
            }
            if(strscpy((char *)macs[i], pBegin, sizeof(mac_address)) < 0)
                pr_loc_wrn("MAC #%d truncated to %zu", i+1, sizeof(mac_address)-1);
            pr_loc_dbg("Set MAC #%d: %s", i+1, (char *)macs[i]);
            pBegin = pEnd + 1;
            pEnd = strchr(pBegin, ',');
            i++;
        }

        if ('\0' != *pBegin && MAX_NET_IFACES > i) {
            macs[i] = kmalloc(sizeof(mac_address), GFP_KERNEL);
            if (unlikely(!macs[i])) {
                pr_loc_crt("kernel memory alloc failure - tried to allocate %lu bytes for macs[%d]", sizeof(mac_address),
                           i);
                goto out_found;
            }
            if(strscpy((char *)macs[i], pBegin, sizeof(mac_address)) < 0)
                pr_loc_wrn("MAC #%d truncated to %zu", i+1, sizeof(mac_address)-1);
            pr_loc_dbg("Set MAC #%d: %s", i+1, (char *)macs[i]);
        }

        goto out_found;
    }

    //mac1=...mac8= are valid options. ASCII for 1 is 49, ASCII for MAX_NET_IFACES is (49 + (MAX_NET_IFACES - 1)) # MAX_NET_IFACES must <=9
    if (strncmp(param_pointer, "mac", 3) != 0 || *(param_pointer + 4) != '=' || *(param_pointer + 3) < 49 ||
        *(param_pointer + 3) > (49 + (MAX_NET_IFACES - 1)))
        return false;

    //Find free spot
    unsigned short i = 0;
    for (; i < MAX_NET_IFACES; i++) {
        if (macs[i])
            continue;

        macs[i] = kmalloc(sizeof(mac_address), GFP_KERNEL);
        if (unlikely(!macs[i])) {
            pr_loc_crt("kernel memory alloc failure - tried to allocate %lu bytes for macs[%d]", sizeof(mac_address),
                       i);
            goto out_found;
        }

        if(strscpy((char *)macs[i], param_pointer + strlen_static(CMDLINE_KT_MAC1), sizeof(mac_address)) < 0)
            pr_loc_wrn("MAC #%d truncated to %zu", i+1, sizeof(mac_address)-1);

        pr_loc_dbg("Set MAC #%d: %s", i+1, (char *)macs[i]);
        goto out_found;
    }

    pr_loc_err("You set more than MAC addresses! Only first %d will be honored.", MAX_NET_IFACES);

    out_found:
        return true;
}

static bool report_unrecognized_option(const char *param_pointer)
{
    pr_loc_dbg("Option \"%s\" not recognized - ignoring", param_pointer);

    return true;
}

/************************************************* End of extractors **************************************************/

static char cmdline_cache[CMDLINE_MAX] = { '\0' };
/**
 * Extracts the cmdline from kernel and caches it for later use
 *
 * The method we use here may seem weird but it is, believe or not, the most direct one available. Kernel cmdline
 * internally is stored in a "saved_command_line" variable (and few derivatives) which isn't exported for modules in
 * any way (at least on x86). The only semi-direct way to get it is to call the method responsible for /proc/cmdline)
 */
static int extract_kernel_cmdline(void)
{
    struct seq_file cmdline_itr = {
        .buf = cmdline_cache,
        .size = CMDLINE_MAX
    };

    int out = _cmdline_proc_show(&cmdline_itr, 0);
    if (out != 0)
        return out;

    pr_loc_dbg("Cmdline count: %d", (unsigned int)cmdline_itr.count);
    if (unlikely(cmdline_itr.count == CMDLINE_MAX)) //if the kernel line is >1K
        pr_loc_wrn("Cmdline may have been truncated to %d", CMDLINE_MAX);

    return 0;
}

/**
 * Returns kernel cmdline up to the length specified by maxlen
 *
 * @param cmdline_out
 * @param maxlen
 * @return 0 on success, -E2BIG if buffer was too small (but the operation succeeded up to maxlen), -E for error
 */
long get_kernel_cmdline(char *cmdline_out, size_t maxlen)
{
    if (unlikely(cmdline_cache[0] == '\0')) {
        int out = extract_kernel_cmdline();
        if (out != 0) {
            pr_loc_err("Failed to extract kernel cmdline");
            return out;
        }
    }

    if (unlikely(maxlen > CMDLINE_MAX))
        maxlen = CMDLINE_MAX;

    return strscpy(cmdline_out, cmdline_cache, maxlen);
}

#define ADD_BLACKLIST_ENTRY(idx, token) kmalloc_or_exit_int(cmdline_blacklist[idx], sizeof(token));  \
                                        strcpy((char *)cmdline_blacklist[idx], token);               \
                                        pr_loc_dbg("Add cmdline blacklist \"%s\" @ %d",              \
                                                   (char *)cmdline_blacklist[idx], idx);
int populate_cmdline_blacklist(cmdline_token *cmdline_blacklist[MAX_BLACKLISTED_CMDLINE_TOKENS], syno_hw *model)
{
    //Currently, this list is static. However, it's prepared to be dynamic based on the model
    //Make sure you don't go over MAX_BLACKLISTED_CMDLINE_TOKENS (and if so adjust it)
    ADD_BLACKLIST_ENTRY(0, CMDLINE_CT_VID);
    ADD_BLACKLIST_ENTRY(1, CMDLINE_CT_PID);
    ADD_BLACKLIST_ENTRY(2, CMDLINE_CT_MFG);
    ADD_BLACKLIST_ENTRY(3, CMDLINE_CT_DOM_SZMAX);
    ADD_BLACKLIST_ENTRY(4, CMDLINE_KT_ELEVATOR);
    ADD_BLACKLIST_ENTRY(5, CMDLINE_KT_LOGLEVEL);
    ADD_BLACKLIST_ENTRY(6, CMDLINE_KT_PK_BUFFER);
    ADD_BLACKLIST_ENTRY(7, CMDLINE_KT_EARLY_PK);
    ADD_BLACKLIST_ENTRY(8, CMDLINE_KT_THAW);

#ifndef NATIVE_SATA_DOM_SUPPORTED //on kernels without SATA DOM support we shouldn't reveal that it's a SATA DOM-boot
    ADD_BLACKLIST_ENTRY(9, CMDLINE_KT_SATADOM);
#endif

    return 0;
}

int extract_config_from_cmdline(struct runtime_config *config)
{
    int out = 0;
    char *cmdline_txt;
    kzalloc_or_exit_int(cmdline_txt, strlen_to_size(CMDLINE_MAX));

    if(get_kernel_cmdline(cmdline_txt, CMDLINE_MAX) <= 0) {
        pr_loc_crt("Failed to extract cmdline");
        out = -EIO;
        goto exit_free;
    }

    pr_loc_dbg("Cmdline: %s", cmdline_txt);

    /**
     * Temporary variables
     */
    unsigned int param_counter = 0;
    char *single_param_chunk; //Pointer to the beginning of the cmdline token
    DBG_ALLOW_UNUSED(param_counter);

    while ((single_param_chunk = strsep(&cmdline_txt, CMDLINE_SEP)) != NULL ) {
        if (unlikely(single_param_chunk[0] == '\0')) //Skip empty params (e.g. last one)
            continue;
        pr_loc_dbg("Param #%d: |%s|", param_counter++, single_param_chunk);

        //Stop after the first one matches
        extract_hw(&config->hw, single_param_chunk)                      ||
        extract_sn(&config->sn, single_param_chunk)                      ||
        extract_boot_media_type(&config->boot_media, single_param_chunk) ||
        extract_vid(&config->boot_media.vid, single_param_chunk)         ||
        extract_pid(&config->boot_media.pid, single_param_chunk)         ||
        extract_dom_max_size(&config->boot_media, single_param_chunk)    ||
        extract_mfg(&config->boot_media.mfg_mode, single_param_chunk)    ||
        extract_port_thaw(&config->port_thaw, single_param_chunk)        ||
        extract_netif_num(&config->netif_num, single_param_chunk)        ||
        extract_netif_macs(config->macs, single_param_chunk)             ||
        report_unrecognized_option(single_param_chunk)                   ;
    }

    if (populate_cmdline_blacklist(config->cmdline_blacklist, &config->hw) != 0) {
        out = -EIO;
        goto exit_free;
    }

    pr_loc_inf("CmdLine processed successfully, tokens=%d", param_counter);

    exit_free:
    kfree(cmdline_txt);
    return out;
}
