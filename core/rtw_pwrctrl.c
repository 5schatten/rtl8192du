/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *
 ******************************************************************************/
#define _RTW_PWRCTRL_C_

#include <autoconf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <osdep_intf.h>

void ips_enter(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	_enter_pwrlock(&pwrpriv->lock);

	pwrpriv->bips_processing = true;

	/*  syn ips_mode with request */
	pwrpriv->ips_mode = pwrpriv->ips_mode_req;

	pwrpriv->ips_enter_cnts++;
	DBG_8192D("==>ips_enter cnts:%d\n", pwrpriv->ips_enter_cnts);

	if (rf_off == pwrpriv->change_rfpwrstate) {
		if (pwrpriv->ips_mode == IPS_LEVEL_2)
			pwrpriv->bkeepfwalive = true;

		rtw_ips_pwr_down(padapter);
		pwrpriv->rf_pwrstate = rf_off;
	}
	pwrpriv->bips_processing = false;
	_exit_pwrlock(&pwrpriv->lock);
}

int ips_leave(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct security_priv *psecuritypriv = &(padapter->securitypriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	int result = _SUCCESS;
	int keyid;
	_enter_pwrlock(&pwrpriv->lock);
	if ((pwrpriv->rf_pwrstate == rf_off) && (!pwrpriv->bips_processing)) {
		pwrpriv->bips_processing = true;
		pwrpriv->change_rfpwrstate = rf_on;
		pwrpriv->ips_leave_cnts++;
		DBG_8192D("==>ips_leave cnts:%d\n", pwrpriv->ips_leave_cnts);

		result = rtw_ips_pwr_up(padapter);
		if (result == _SUCCESS)
			pwrpriv->rf_pwrstate = rf_on;

		if ((_WEP40_ == psecuritypriv->dot11PrivacyAlgrthm) ||
		    (_WEP104_ == psecuritypriv->dot11PrivacyAlgrthm)) {
			DBG_8192D("==>%s,channel(%d),processing(%x)\n",
				  __func__, padapter->mlmeextpriv.cur_channel,
				  pwrpriv->bips_processing);
			set_channel_bwmode(padapter,
					   padapter->mlmeextpriv.cur_channel,
					   HAL_PRIME_CHNL_OFFSET_DONT_CARE,
					   HT_CHANNEL_WIDTH_20);
			for (keyid = 0; keyid < 4; keyid++) {
				if (pmlmepriv->key_mask & BIT(keyid)) {
					if (keyid ==
					    psecuritypriv->dot11PrivacyKeyIndex)
						result =
						    rtw_set_key(padapter,
								psecuritypriv,
								keyid, 1);
					else
						result =
						    rtw_set_key(padapter,
								psecuritypriv,
								keyid, 0);
				}
			}
		}

		DBG_8192D("==> ips_leave.....LED(0x%08x)...\n",
			  rtw_read32(padapter, 0x4c));
		pwrpriv->bips_processing = false;

		pwrpriv->bkeepfwalive = false;
	}
	_exit_pwrlock(&pwrpriv->lock);
	return result;
}

static bool rtw_pwr_unassociated_idle(struct rtw_adapter *adapter)
{
	struct rtw_adapter *buddy = adapter->pbuddy_adapter;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &(adapter->wdinfo);
#ifdef CONFIG_IOCTL_CFG80211
	struct cfg80211_wifidirect_info *pcfg80211_wdinfo =
	    &adapter->cfg80211_wdinfo;
#endif
#endif

	bool ret = false;

	if (adapter->pwrctrlpriv.ips_deny_time >= rtw_get_current_time()) {
		/* DBG_8192D("%s ips_deny_time\n", __func__); */
		goto exit;
	}

	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE | WIFI_SITE_MONITOR) ||
	    check_fwstate(pmlmepriv, WIFI_UNDER_LINKING | WIFI_UNDER_WPS) ||
	    check_fwstate(pmlmepriv, WIFI_AP_STATE) ||
#if defined(CONFIG_P2P)
	    !rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE) ||
#endif
	    check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE | WIFI_ADHOC_STATE))
		goto exit;

	/* consider buddy, if exist */
	if (buddy) {
		struct mlme_priv *b_pmlmepriv = &(buddy->mlmepriv);
#ifdef CONFIG_P2P
		struct wifidirect_info *b_pwdinfo = &(buddy->wdinfo);
#ifdef CONFIG_IOCTL_CFG80211
		struct cfg80211_wifidirect_info *b_pcfg80211_wdinfo =
		    &buddy->cfg80211_wdinfo;
#endif
#endif

		if (check_fwstate(b_pmlmepriv, WIFI_ASOC_STATE | WIFI_SITE_MONITOR) ||
		    check_fwstate(b_pmlmepriv, WIFI_UNDER_LINKING | WIFI_UNDER_WPS) ||
		    check_fwstate(b_pmlmepriv, WIFI_AP_STATE) ||
#if defined(CONFIG_P2P)
		    !rtw_p2p_chk_state(b_pwdinfo, P2P_STATE_NONE) ||
#endif
		    check_fwstate(b_pmlmepriv, WIFI_ADHOC_MASTER_STATE | WIFI_ADHOC_STATE)) {
			goto exit;
		}
	}
	ret = true;

exit:
	return ret;
}

void rtw_ps_processor(struct rtw_adapter *padapter)
{
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &(padapter->wdinfo);
#endif /* CONFIG_P2P */
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	pwrpriv->ps_processing = true;

	if (pwrpriv->ips_mode_req == IPS_NONE)
		goto exit;

	if (rtw_pwr_unassociated_idle(padapter) == false)
		goto exit;

	if ((pwrpriv->rf_pwrstate == rf_on) &&
	    ((pwrpriv->pwr_state_check_cnts % 4) == 0)) {
		DBG_8192D("==>%s .fw_state(%x)\n", __func__,
			  get_fwstate(pmlmepriv));
		pwrpriv->change_rfpwrstate = rf_off;

		ips_enter(padapter);
	}
exit:
	rtw_set_pwr_state_check_timer(&padapter->pwrctrlpriv);
	pwrpriv->ps_processing = false;
	return;
}

static void pwr_state_check_handler(void *FunctionContext)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *)FunctionContext;

	rtw_ps_cmd(padapter);
}

/*
 *
 * Parameters
 *	padapter
 *	pslv			power state level, only could be PS_STATE_S0 ~ PS_STATE_S4
 *
 */
void rtw_set_rpwm(struct rtw_adapter *padapter, u8 pslv)
{
	u8 rpwm;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;

	pslv = PS_STATE(pslv);

	if (pwrpriv->rpwm == pslv) {
		RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_err_,
			 ("%s: Already set rpwm[0x%02x]!\n", __func__, pslv));
		return;
	}

	if ((padapter->bDriverStopped == true) ||
	    (padapter->bSurpriseRemoved == true)) {
		RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_err_,
			 ("%s: bDriverStopped(%d) bSurpriseRemoved(%d)\n",
			  __func__, padapter->bDriverStopped,
			  padapter->bSurpriseRemoved));
		return;
	}

	rpwm = pslv | pwrpriv->tog;
	RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_notice_,
		 ("rtw_set_rpwm: rpwm=0x%02x cpwm=0x%02x\n", rpwm,
		  pwrpriv->cpwm));

	pwrpriv->rpwm = pslv;

	rtw_hal_set_hwreg(padapter, HW_VAR_SET_RPWM, (u8 *)(&rpwm));

	pwrpriv->tog += 0x80;

	if (!(rpwm & PS_ACK))
		pwrpriv->cpwm = pslv;

}

static u8 ps_rdy_check(struct rtw_adapter *padapter)
{
	u32 curr_time, delta_time;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	curr_time = rtw_get_current_time();

	delta_time = curr_time - pwrpriv->DelayLPSLastTimeStamp;

	if (delta_time < LPS_DELAY_TIME)
		return false;

	if ((check_fwstate(pmlmepriv, _FW_LINKED) == false) ||
	    (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true))
		return false;

	if (true == pwrpriv->bInSuspend)
		return false;

	if ((padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) &&
	    (padapter->securitypriv.binstallGrpkey == false)) {
		DBG_8192D("Group handshake still in progress !!!\n");
		return false;
	}
#ifdef CONFIG_IOCTL_CFG80211
	if (!rtw_cfg80211_pwr_mgmt(padapter))
		return false;
#endif

	return true;
}

void rtw_set_ps_mode(struct rtw_adapter *padapter, u8 ps_mode, u8 smart_ps)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &(padapter->wdinfo);
#endif /* CONFIG_P2P */

	RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_notice_,
		 ("%s: PowerMode=%d Smart_PS=%d\n",
		  __func__, ps_mode, smart_ps));

	if (ps_mode > PM_Card_Disable) {
		RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_err_,
			 ("ps_mode:%d error\n", ps_mode));
		return;
	}

	if ((pwrpriv->pwr_mode == ps_mode) && (pwrpriv->smart_ps == smart_ps)) {
		return;
	}

	/* if (pwrpriv->pwr_mode == PS_MODE_ACTIVE) */
	if (ps_mode == PS_MODE_ACTIVE) {
		DBG_8192D
		    ("rtw_set_ps_mode(): Busy Traffic , Leave 802.11 power save..\n");

		pwrpriv->smart_ps = smart_ps;
		pwrpriv->pwr_mode = ps_mode;

		rtw_set_rpwm(padapter, PS_STATE_S4);
		rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE,
				  (u8 *)(&ps_mode));
		pwrpriv->bFwCurrentInPSMode = false;
	} else {
		if (ps_rdy_check(padapter)) {
			DBG_8192D
			    ("rtw_set_ps_mode(): Enter 802.11 power save mode...\n");

			pwrpriv->smart_ps = smart_ps;
			pwrpriv->pwr_mode = ps_mode;
			pwrpriv->bFwCurrentInPSMode = true;
			rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE, (u8 *)(&ps_mode));
			rtw_set_rpwm(padapter, PS_STATE_S2);
		}
	}
}

/*	Description: */
/*		Enter the leisure power save mode. */
void rtw_lps_enter(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct rtw_adapter *buddy = padapter->pbuddy_adapter;

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->iface_type != IFACE_PORT0)
		return;		/* Skip power saving for concurrent mode port 1 */

	/* consider buddy, if exist */
	if (buddy) {
		struct mlme_priv *b_pmlmepriv = &(buddy->mlmepriv);
#ifdef CONFIG_P2P
		struct wifidirect_info *b_pwdinfo = &(buddy->wdinfo);
#ifdef CONFIG_IOCTL_CFG80211
		struct cfg80211_wifidirect_info *b_pcfg80211_wdinfo =
		    &buddy->cfg80211_wdinfo;
#endif
#endif

		if (check_fwstate
		    (b_pmlmepriv, WIFI_ASOC_STATE | WIFI_SITE_MONITOR) ||
		     check_fwstate(b_pmlmepriv, WIFI_UNDER_LINKING | WIFI_UNDER_WPS) ||
		     check_fwstate(b_pmlmepriv, WIFI_AP_STATE) ||
		     check_fwstate(b_pmlmepriv, WIFI_ADHOC_MASTER_STATE | WIFI_ADHOC_STATE) ||
#if defined(CONFIG_P2P)
		    !rtw_p2p_chk_state(b_pwdinfo, P2P_STATE_NONE) ||
#endif
		    rtw_is_scan_deny(buddy))
			return;
	}
#endif
	if ((check_fwstate(pmlmepriv, _FW_LINKED) == false) ||
	    (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == true) ||
	    (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true))
		return;

	if (true == pwrpriv->bInSuspend)
		return;

	if (pwrpriv->bLeisurePs) {
		/*  Idle for a while if we connect to AP a while ago. */
		if (pwrpriv->LpsIdleCount >= 2) {	/*   4 Sec */
			if (pwrpriv->pwr_mode == PS_MODE_ACTIVE) {
				rtw_set_ps_mode(padapter, pwrpriv->power_mgnt,
						2);
			}
		} else {
			pwrpriv->LpsIdleCount++;
		}
	}
}

/*  */
/*	Description: */
/*		Leave the leisure power save mode. */
/*  */
void rtw_lps_leave(struct rtw_adapter *padapter)
{
#define LPS_LEAVE_TIMEOUT_MS 100

	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	u32 start_time;
	bool bAwake = false;

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->iface_type != IFACE_PORT0)
		return;		/* Skip power saving for concurrent mode port 1 */
#endif

	if (pwrpriv->bLeisurePs) {
		if (pwrpriv->pwr_mode != PS_MODE_ACTIVE) {
			rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0);

			if (pwrpriv->pwr_mode == PS_MODE_ACTIVE) {
				start_time = rtw_get_current_time();
				while (1) {
					rtw_hal_get_hwreg(padapter,
							  HW_VAR_FWLPS_RF_ON,
							  (u8 *)(&bAwake));

					if (bAwake || padapter->bSurpriseRemoved)
						break;

					if (rtw_get_passing_time_ms(start_time) > LPS_LEAVE_TIMEOUT_MS) {
						DBG_8192D
						    ("Wait for FW LPS leave more than %u ms!!!\n",
						     LPS_LEAVE_TIMEOUT_MS);
						break;
					}
					rtw_usleep_os(100);
				}
			}
		}
	}
}

/*  Description: Leave all power save mode: LPS, FwLPS, IPS if needed. */
/*  Move code to function by tynli. 2010.03.26. */
/*  */
void LeaveAllPowerSaveMode(struct rtw_adapter *adapter)
{
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);

	if (check_fwstate(pmlmepriv, _FW_LINKED))
		rtw_lps_leave(adapter);
}

void rtw_init_pwrctrl_priv(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	_init_pwrlock(&pwrctrlpriv->lock);
	pwrctrlpriv->rf_pwrstate = rf_on;
	pwrctrlpriv->ips_enter_cnts = 0;
	pwrctrlpriv->ips_leave_cnts = 0;

	pwrctrlpriv->ips_mode = padapter->registrypriv.ips_mode;
	pwrctrlpriv->ips_mode_req = padapter->registrypriv.ips_mode;

	pwrctrlpriv->pwr_state_check_interval = RTW_PWR_STATE_CHK_INTERVAL;
	pwrctrlpriv->pwr_state_check_cnts = 0;
	pwrctrlpriv->bInternalAutoSuspend = false;
	pwrctrlpriv->bInSuspend = false;
	pwrctrlpriv->bkeepfwalive = false;

	pwrctrlpriv->LpsIdleCount = 0;
	pwrctrlpriv->power_mgnt = padapter->registrypriv.power_mgnt;	/*  PS_MODE_MIN; */
	pwrctrlpriv->bLeisurePs =
	    (PS_MODE_ACTIVE != pwrctrlpriv->power_mgnt) ? true : false;

	pwrctrlpriv->bFwCurrentInPSMode = false;

	pwrctrlpriv->cpwm = PS_STATE_S4;

	pwrctrlpriv->pwr_mode = PS_MODE_ACTIVE;

	pwrctrlpriv->smart_ps = 0;

	pwrctrlpriv->tog = 0x80;

	_init_timer(&(pwrctrlpriv->pwr_state_check_timer), padapter->pnetdev,
		    pwr_state_check_handler, (u8 *)padapter);
}

void rtw_free_pwrctrl_priv(struct rtw_adapter *adapter)
{
	struct pwrctrl_priv *pwrctrlpriv = &adapter->pwrctrlpriv;

	_free_pwrlock(&pwrctrlpriv->lock);
}

u8 rtw_interface_ps_func(struct rtw_adapter *padapter,
			 enum HAL_INTF_PS_FUNC efunc_id, u8 *val)
{
	rtw_hal_intf_ps_func(padapter, efunc_id, val);
	return true;
}

inline void rtw_set_ips_deny(struct rtw_adapter *padapter, u32 ms)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	pwrpriv->ips_deny_time = rtw_get_current_time() + rtw_ms_to_systime(ms);
}

/*
* rtw_pwr_wakeup - Wake the NIC up from: 1)IPS. 2)USB autosuspend
* @adapter: pointer to _adapter structure
* @ips_deffer_ms: the ms wiil prevent from falling into IPS after wakeup
* Return _SUCCESS or _FAIL
*/
int _rtw_pwr_wakeup(struct rtw_adapter *padapter, u32 ips_deffer_ms,
		    const char *caller)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int ret = _SUCCESS;
	u32 start = rtw_get_current_time();

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->pbuddy_adapter)
		LeaveAllPowerSaveMode(padapter->pbuddy_adapter);

	if ((padapter->isprimary == false) && padapter->pbuddy_adapter) {
		padapter = padapter->pbuddy_adapter;
		pwrpriv = &padapter->pwrctrlpriv;
		pmlmepriv = &padapter->mlmepriv;
	}
#endif

	if (pwrpriv->ips_deny_time <
	    rtw_get_current_time() + rtw_ms_to_systime(ips_deffer_ms))
		pwrpriv->ips_deny_time =
		    rtw_get_current_time() + rtw_ms_to_systime(ips_deffer_ms);

	if (pwrpriv->ps_processing) {
		DBG_8192D("%s wait ps_processing...\n", __func__);
		while (pwrpriv->ps_processing &&
		       rtw_get_passing_time_ms(start) <= 3000)
			rtw_msleep_os(10);
		if (pwrpriv->ps_processing)
			DBG_8192D("%s wait ps_processing timeout\n", __func__);
		else
			DBG_8192D("%s wait ps_processing done\n", __func__);
	}

	if (pwrpriv->bInternalAutoSuspend == false && pwrpriv->bInSuspend) {
		DBG_8192D("%s wait bInSuspend...\n", __func__);
		while (pwrpriv->bInSuspend &&
		       ((rtw_get_passing_time_ms(start) <= 3000)))
			rtw_msleep_os(10);
		if (pwrpriv->bInSuspend)
			DBG_8192D("%s wait bInSuspend timeout\n", __func__);
		else
			DBG_8192D("%s wait bInSuspend done\n", __func__);
	}

	/* System suspend is not allowed to wakeup */
	if ((pwrpriv->bInternalAutoSuspend == false) &&
	    (true == pwrpriv->bInSuspend)) {
		ret = _FAIL;
		goto exit;
	}

	/* block??? */
	if ((pwrpriv->bInternalAutoSuspend == true) &&
	    (padapter->net_closed == true)) {
		ret = _FAIL;
		goto exit;
	}

	/* I think this should be check in IPS, LPS, autosuspend functions... */
	if (check_fwstate(pmlmepriv, _FW_LINKED) == true) {
		ret = _SUCCESS;
		goto exit;
	}

	if (rf_off == pwrpriv->rf_pwrstate) {
		DBG_8192D("%s call ips_leave....\n", __func__);
		if (_FAIL == ips_leave(padapter)) {
			DBG_8192D
			    ("======> ips_leave fail.............\n");
			ret = _FAIL;
			goto exit;
		}
	}

	/* TODO: the following checking need to be merged... */
	if (padapter->bDriverStopped || !padapter->bup ||
	    !padapter->hw_init_completed) {
		DBG_8192D
		    ("%s: bDriverStopped=%d, bup=%d, hw_init_completed=%u\n",
		     caller, padapter->bDriverStopped, padapter->bup,
		     padapter->hw_init_completed);
		ret = false;
		goto exit;
	}

exit:
	if (pwrpriv->ips_deny_time <
	    rtw_get_current_time() + rtw_ms_to_systime(ips_deffer_ms))
		pwrpriv->ips_deny_time =
		    rtw_get_current_time() + rtw_ms_to_systime(ips_deffer_ms);
	return ret;
}

int rtw_pm_set_lps(struct rtw_adapter *padapter, u8 mode)
{
	int ret = 0;
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	if (mode < PS_MODE_NUM) {
		if (pwrctrlpriv->power_mgnt != mode) {
			if (PS_MODE_ACTIVE == mode)
				LeaveAllPowerSaveMode(padapter);
			else
				pwrctrlpriv->LpsIdleCount = 2;
			pwrctrlpriv->power_mgnt = mode;
			pwrctrlpriv->bLeisurePs =
			    (PS_MODE_ACTIVE !=
			     pwrctrlpriv->power_mgnt) ? true : false;
		}
	} else {
		ret = -EINVAL;
	}

	return ret;
}

int rtw_pm_set_ips(struct rtw_adapter *padapter, u8 mode)
{
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	if (mode == IPS_NORMAL || mode == IPS_LEVEL_2) {
		rtw_ips_mode_req(pwrctrlpriv, mode);
		DBG_8192D("%s %s\n", __func__,
			  mode == IPS_NORMAL ? "IPS_NORMAL" : "IPS_LEVEL_2");
		return 0;
	} else if (mode == IPS_NONE) {
		rtw_ips_mode_req(pwrctrlpriv, mode);
		DBG_8192D("%s %s\n", __func__, "IPS_NONE");
		if ((padapter->bSurpriseRemoved == 0) &&
		    (_FAIL == rtw_pwr_wakeup(padapter)))
			return -EFAULT;
	} else {
		return -EINVAL;
	}
	return 0;
}
