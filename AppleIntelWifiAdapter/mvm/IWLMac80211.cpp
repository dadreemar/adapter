//
//  IWLMac80211.cpp
//  AppleIntelWifiAdapter
//
//  Created by 钟先耀 on 2020/2/21.
//  Copyright © 2020 钟先耀. All rights reserved.
//

#include "IWLMvmDriver.hpp"

bool IWLMvmDriver::ieee80211Init()
{
    struct ieee80211com *ic = &m_pDevice->ie_ic;
    struct ifnet *ifp = &ic->ic_if;
    ic->ic_phytype = IEEE80211_T_OFDM;    /* not only, but not used */
    ic->ic_opmode = IEEE80211_M_STA;    /* default to BSS mode */
    ic->ic_state = IEEE80211_S_INIT;
    /* Set device capabilities. */
    ic->ic_caps =
    IEEE80211_C_WEP |        /* WEP */
    IEEE80211_C_RSN |        /* WPA/RSN */
    IEEE80211_C_SCANALL |    /* device scans all channels at once */
    IEEE80211_C_SCANALLBAND |    /* device scans all bands at once */
    IEEE80211_C_SHSLOT |    /* short slot time supported */
    IEEE80211_C_SHPREAMBLE;    /* short preamble supported */
    ic->ic_htcaps = IEEE80211_HTCAP_SGI20;
    ic->ic_htcaps |=
    (IEEE80211_HTCAP_SMPS_DIS << IEEE80211_HTCAP_SMPS_SHIFT);
    ic->ic_htxcaps = 0;
    ic->ic_txbfcaps = 0;
    ic->ic_aselcaps = 0;
    ic->ic_ampdu_params = (IEEE80211_AMPDU_PARAM_SS_4 | 0x3 /* 64k */);
    
    ic->ic_sup_rates[IEEE80211_MODE_11A] = ieee80211_std_rateset_11a;
    ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
    ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;
    m_pDevice->ie_amrr.amrr_min_success_threshold =  1;
    m_pDevice->ie_amrr.amrr_max_success_threshold = 15;
    /* IBSS channel undefined for now. */
    ic->ic_ibss_chan = &ic->ic_channels[1];
    ic->ic_max_rssi = IWM_MAX_DBM - IWM_MIN_DBM;
    ifp->if_softc = this;
    ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
    //    ifp->if_ioctl = iwm_ioctl;
    //    ifp->if_start = iwm_start;
    //    ifp->if_watchdog = iwm_watchdog;
    memcpy(ifp->if_xname, DRIVER_NAME, IFNAMSIZ);
    //#if NBPFILTER > 0
    //    iwm_radiotap_attach(sc);
    //#endif
    //    timeout_set(&sc->sc_calib_to, iwm_calib_timeout, sc);
    //    timeout_set(&sc->sc_led_blink_to, iwm_led_blink_timeout, sc);
    //    task_set(&sc->init_task, iwm_init_task, sc);
    //    task_set(&sc->newstate_task, iwm_newstate_task, sc);
    //    task_set(&sc->ba_task, iwm_ba_task, sc);
    //    task_set(&sc->htprot_task, iwm_htprot_task, sc);
    ieee80211_ifattach(ifp);
    ifp->iface = m_pDevice->interface;
    ieee80211_media_init(ifp);
    ic->ic_node_alloc = OSMemberFunctionCast(NodeAllocAction, this, &IWLMvmDriver::iwm_node_alloc);
    ic->ic_bgscan_start = OSMemberFunctionCast(BgScanAction, this, &IWLMvmDriver::iwm_bgscan);
    return true;
}

bool IWLMvmDriver::ieee80211Run()
{
    struct ieee80211com *ic = &m_pDevice->ie_ic;
    struct ifnet *ifp = &ic->ic_if;
    /* Update MAC in case the upper layers changed it. */
    if (!m_pDevice->nvm_data) {
        IWL_ERR(0, "No MAC address, start fail\n");
        return false;
    }
    IEEE80211_ADDR_COPY(((struct arpcom *)ifp)->ac_enaddr, m_pDevice->nvm_data->hw_addr);
    IEEE80211_ADDR_COPY(ic->ic_myaddr,
        ((struct arpcom *)ifp)->ac_enaddr);
    if (m_pDevice->nvm_data->sku_cap_11n_enable)
        iwm_setup_ht_rates();
    /* not all hardware can do 5GHz band */
    if (!m_pDevice->nvm_data->sku_cap_band_52ghz_enable)
        memset(&ic->ic_sup_rates[IEEE80211_MODE_11A], 0,
            sizeof(ic->ic_sup_rates[IEEE80211_MODE_11A]));
    /* Configure channel information obtained from firmware. */
    ieee80211_channel_init(ifp);
    ieee80211_media_init(ifp);
    
//    ieee80211_begin_scan(ifp);
    struct iwm_node *in = (struct iwm_node *)m_pDevice->ie_ic.ic_bss;
    ieee80211_amrr_node_init(&m_pDevice->ie_amrr, &in->in_amn);
    ieee80211_mira_node_init(&in->in_mn);
    /* Start at lowest available bit-rate, AMRR will raise. */
    in->in_ni.ni_txrate = 0;
    in->in_ni.ni_txmcs = 0;
    return true;
}

int IWLMvmDriver::iwm_bgscan(struct ieee80211com *ic)
{
    IWL_INFO(0, "bg scan\n");
    return 0;
}

struct ieee80211_node *IWLMvmDriver::iwm_node_alloc(struct ieee80211com *ic)
{
    IWL_INFO(0, "iwm_node_alloc\n");
    void *buf = IOMalloc(sizeof(struct iwm_node));
    if (buf) {
        bzero(buf, sizeof(struct iwm_node));
    }
    return (struct ieee80211_node*)buf;
}

void IWLMvmDriver::iwm_setup_ht_rates()
{
    struct ieee80211com *ic = &m_pDevice->ie_ic;
    uint8_t rx_ant;

    /* TX is supported with the same MCS as RX. */
    ic->ic_tx_mcs_set = IEEE80211_TX_MCS_SET_DEFINED;

    ic->ic_sup_mcs[0] = 0xff;        /* MCS 0-7 */

    if (m_pDevice->nvm_data->sku_cap_mimo_disabled)
        return;

    rx_ant = iwl_mvm_get_valid_rx_ant(m_pDevice);
    if ((rx_ant & ANT_AB) == ANT_AB ||
        (rx_ant & ANT_BC) == ANT_BC)
        ic->ic_sup_mcs[1] = 0xff;    /* MCS 8-15 */
}
