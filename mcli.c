/*
 * (c) BayCom GmbH, http://www.baycom.de, info@baycom.de
 *
 * See the COPYING file for copyright information and
 * how to reach the author.
 *
 */

/*
 * mcli.c: A plugin for the Video Disk Recorder
 */

#include <vdr/plugin.h>
#include <vdr/player.h>

#include "filter.h"
#include "device.h"
#include "cam_menu.h"
#include "mcli_service.h"
#include "mcli.h"

static int reconf = 0;

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------

class cMenuSetupMcli:public cMenuSetupPage
{
      private:
	cmdline_t * m_cmd;
      protected:
	virtual void Store (void);
      public:
	cMenuSetupMcli (cmdline_t * cmd);
};

cMenuSetupMcli::cMenuSetupMcli (cmdline_t * cmd)
{
	m_cmd = cmd;
	Add (new cMenuEditIntItem (trNOOP ("DVB-C"), &m_cmd->tuner_type_limit[FE_QAM]));
	Add (new cMenuEditIntItem (trNOOP ("DVB-T"), &m_cmd->tuner_type_limit[FE_OFDM]));
	Add (new cMenuEditIntItem (trNOOP ("DVB-S"), &m_cmd->tuner_type_limit[FE_QPSK]));
	Add (new cMenuEditIntItem (trNOOP ("DVB-S2"), &m_cmd->tuner_type_limit[FE_DVBS2]));
}

void cMenuSetupMcli::Store (void)
{
	SetupStore ("DVB-C", m_cmd->tuner_type_limit[FE_QAM]);
	SetupStore ("DVB-T", m_cmd->tuner_type_limit[FE_OFDM]);
	SetupStore ("DVB-S", m_cmd->tuner_type_limit[FE_QPSK]);
	SetupStore ("DVB-S2", m_cmd->tuner_type_limit[FE_DVBS2]);
	reconf = 1;
}

cOsdObject *cPluginMcli::AltMenuAction (void)
{
	// Call this code periodically to find out if any CAM out there want's us to tell something.
	// If it's relevant to us we need to check if any of our DVB-Devices gets programm from a NetCeiver with this UUID.
	// The text received should pop up via OSD with a CAM-Session opened afterwards (CamMenuOpen...CamMenuReceive...CamMenuSend...CamMenuClose).
	mmi_info_t m;
	if (CamPollText (&m) > 0) {
		printf ("NetCeiver %s CAM slot %d Received %s valid for:\n", m.uuid, m.slot, m.mmi_text);
		for (int i = 0; i < m.caid_num; i++) {
			caid_mcg_t *c = m.caids + i;
			int satpos;
			fe_type_t type;
			recv_sec_t sec;
			struct dvb_frontend_parameters fep;
			int vpid;

			mcg_get_satpos (&c->mcg, &satpos);
			mcg_to_fe_parms (&c->mcg, &type, &sec, &fep, &vpid);

			for (cMcliDeviceObject * dev = m_devs.First (); dev; dev = m_devs.Next (dev)) {
				cMcliDevice *d = dev->d ();
				//printf("satpos: %i vpid: %i fep.freq: %i dev.freq: %i\n", satpos, vpid, fep.frequency, dev->CurChan()->Frequency());
				struct in6_addr mcg = d->GetTenData ()->mcg;
				mcg_set_id (&mcg, 0);

#if 1				//def DEBUG
				char str[INET6_ADDRSTRLEN];
				inet_ntop (AF_INET6, &c->mcg, str, INET6_ADDRSTRLEN);
				printf ("MCG from MMI: %s\n", str);
				inet_ntop (AF_INET6, &mcg, str, INET6_ADDRSTRLEN);
				printf ("MCG from DEV: %s\n", str);
#endif

				if (IN6_IS_ADDR_UNSPECIFIED (&c->mcg) || !memcmp (&c->mcg, &mcg, sizeof (struct in6_addr)))
					return new cCamMenu (&m_cmd, &m);
			}
			printf ("SID/Program Number:%04x, SatPos:%d Freqency:%d\n", c->caid, satpos, fep.frequency);
		}
		if (m.caid_num && m.caids) {
			free (m.caids);
		}
	}
	return NULL;
}

int cPluginMcli::CamPollText (mmi_info_t * text)
{
	if (m_mmi_init_done && !reconf) {
		return mmi_poll_for_menu_text (m_cam_mmi, text, 10);
	} else {
		return 0;
	}
}

cPluginMcli::cPluginMcli (void)
{
	// printf ("cPluginMcli::cPluginMcli\n");
	int i;
	//init parameters
	memset (&m_cmd, 0, sizeof (cmdline_t));

	for (i = 0; i <= FE_DVBS2; i++) {
		m_cmd.tuner_type_limit[i] = MCLI_MAX_DEVICES;
	}
	m_cmd.port = 23000;
	m_cmd.mld_start = 1;
	m_mmi_init_done = 0;
	m_recv_init_done = 0;
	m_mld_init_done = 0;
	m_api_init_done = 0;
	memset (m_cam_pool, 0, sizeof (cam_pool_t) * CAM_POOL_MAX);
	for(i=0; i<CAM_POOL_MAX; i++) {
		m_cam_pool[i].max = -1;
	}
	strcpy (m_cmd.cmd_sock_path, API_SOCK_NAMESPACE);
	memset (m_tuner_pool, 0, sizeof(tuner_pool_t)*TUNER_POOL_MAX);
	for(i=0; i<TUNER_POOL_MAX; i++) {
		m_tuner_pool[i].type = -1;
	}
}

cPluginMcli::~cPluginMcli ()
{
//	printf ("cPluginMcli::~cPluginMcli\n");
	ExitMcli ();

}

bool cPluginMcli::InitMcli (void)
{
	if (!recv_init (m_cmd.iface, m_cmd.port)) {
		m_recv_init_done = 1;
	}
	if (m_cmd.mld_start && !mld_client_init (m_cmd.iface)) {
		m_mld_init_done = 1;
	}
	if (!api_sock_init (m_cmd.cmd_sock_path)) {
		m_api_init_done = 1;
	}
	m_cam_mmi = mmi_broadcast_client_init (m_cmd.port, m_cmd.iface);
	if (m_cam_mmi > 0) {
		m_mmi_init_done = 1;
	}
	return true;
}

void cPluginMcli::ExitMcli (void)
{
	if (m_mmi_init_done) {
		mmi_broadcast_client_exit (m_cam_mmi);
	}
	if (m_api_init_done) {
		api_sock_exit ();
	}
	if (m_mld_init_done) {
		mld_client_exit ();
	}
	if (m_recv_init_done) {
		recv_exit ();
	}
}

const char *cPluginMcli::CommandLineHelp (void)
{
	return ("  --ifname <network interface>\n" "  --port <port> (default: -port 23000)\n" "  --dvb-s <num> --dvb-c <num> --dvb-t <num> --atsc <num> --dvb-s2 <num>\n" "    limit number of device types (default: 8 of every type)\n" "  --mld-reporter-disable\n" "  --sock-path <filepath>\n" "\n");
}

bool cPluginMcli::ProcessArgs (int argc, char *argv[])
{
//	printf ("cPluginMcli::ProcessArgs\n");
	int tuners = 0, i;
	char c;
	int ret;

	while (1) {
		//int this_option_optind = optind ? optind : 1;
		int option_index = 0;
		static struct option long_options[] = {
			{"port", 1, 0, 0},	//0
			{"ifname", 1, 0, 0},	//1
			{"dvb-s", 1, 0, 0},	//2
			{"dvb-c", 1, 0, 0},	//3
			{"dvb-t", 1, 0, 0},	//4
			{"atsc", 1, 0, 0},	//5
			{"dvb-s2", 1, 0, 0},	//6
			{"mld-reporter-disable", 0, 0, 0},	//7
			{"sock-path", 1, 0, 0},	//8
			{NULL, 0, 0, 0}
		};

		ret = getopt_long_only (argc, argv, "", long_options, &option_index);
		c = (char) ret;
		if (ret == -1 || c == '?') {
			break;
		}

		switch (option_index) {
		case 0:
			m_cmd.port = atoi (optarg);
			break;
		case 1:
			strncpy (m_cmd.iface, optarg, IFNAMSIZ - 1);
			break;
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
			i = atoi (optarg);
			if (!tuners) {
				memset (m_cmd.tuner_type_limit, 0, sizeof (m_cmd.tuner_type_limit));
			}
			m_cmd.tuner_type_limit[option_index - 2] = i;
			tuners += i;
			break;
		case 7:
			m_cmd.mld_start = 0;
			break;
		case 8:
			strncpy (m_cmd.cmd_sock_path, optarg, _POSIX_PATH_MAX - 1);
			break;
		default:
			printf ("?? getopt returned character code 0%o ??\n", c);
		}
	}
	// Implement command line argument processing here if applicable.
	return true;
}

cam_pool_t *cPluginMcli::CAMFindByUUID (const char *uuid, int slot)
{
	cam_pool_t *cp;
	for(int i=0; i<CAM_POOL_MAX; i++) {
		cp = m_cam_pool + i;
		if(cp->max >= 0 && !strcmp(cp->uuid, uuid) && (slot == -1 || slot == cp->slot)) {
			return cp;
		}
	}
	return NULL;
}

cam_pool_t *cPluginMcli::CAMPoolFindFree(void)
{
	for(int i=0; i<CAM_POOL_MAX; i++) {
		cam_pool_t *cp = m_cam_pool + i;
		if(cp->max == -1) {
			return cp;
		}
	}
	return NULL;
}
int cPluginMcli::CAMPoolAdd(netceiver_info_t *nci)
{
	bool update = false;
	int ret = 0;
	for(int j=0; j < nci->cam_num; j++) {
		update = false;
		cam_pool_t *cp=CAMFindByUUID(nci->uuid, nci->cam[j].slot);
		if(!cp) {
			cp=CAMPoolFindFree();
			if(!ret) {
				ret = 1;
			}
		} else {
			update = true;
			ret = 2;
		}
		if(!cp){
			return ret;
		}
		if (nci->cam[j].status) {
			switch (nci->cam[j].flags) {
				case CA_SINGLE:
				case CA_MULTI_SID:
					cp->max = 1;
					break;
				case CA_MULTI_TRANSPONDER:
					cp->max = nci->cam[j].max_sids/* - nci->cam[j].use_sids*/;
					break;
			}
		} else {
			cp->max = 0;
		}
		if(!update) {
			cp->slot = nci->cam[j].slot;
			strcpy(cp->uuid, nci->uuid);
			cp->use = 0;
		}
	}
	return ret;
}
bool cPluginMcli::CAMPoolDel(const char *uuid)
{
	cam_pool_t *cp;
	bool ret=false;
	for(int i=0; i<CAM_POOL_MAX; i++) {
		cp = m_cam_pool + i;
		if(cp->max>=0 && !strcmp(cp->uuid, uuid)) {
			cp->max = -1;
			ret=true;
		}
	}
	return ret;
}

cam_pool_t *cPluginMcli::CAMAvailable (const char *uuid, int slot, bool lock)
{
	cam_pool_t *ret = NULL;
	if(lock) {
		Lock();
	}
	cam_pool_t *cp;
	for(int i=0; i<CAM_POOL_MAX; i++) {
		cp = m_cam_pool + i;
		if(cp->max>0 && (!uuid || !strcmp(cp->uuid, uuid)) && (slot == -1 || (cp->slot == slot))) {
			if((cp->max - cp->use) > 0){
				ret = cp;
				break;
			}
		}
	}
#ifdef DEBUG_RESOURCES
	if(ret) {
		printf("CAMAvailable %s %d -> %s %d\n", uuid, slot, ret->uuid, ret->slot);
	}
#endif
	if(lock) {
		Unlock();
	}
	return ret;
}
cam_pool_t *cPluginMcli::CAMAlloc (const char *uuid, int slot)
{
	LOCK_THREAD;
#ifdef DEBUG_RESOURCES
	printf ("Alloc CAM %s %d\n", uuid, slot);
#endif
	cam_pool_t *cp;
	if ((cp = CAMAvailable (uuid, slot, false))) {
		cp->use++;
		return cp;
	}
	return NULL;
}
int cPluginMcli::CAMFree (cam_pool_t *cp)
{
	LOCK_THREAD;
#ifdef DEBUG_RESOURCES
	printf ("FreeCAM %s %d\n", cp->uuid, cp->slot);
#endif
	if (cp->use > 0) {
		cp->use--;
	}
	return cp->use;
}
bool cPluginMcli::CAMSteal(const char *uuid, int slot, bool force)
{
		for (cMcliDeviceObject * d = m_devs.First (); d; d = m_devs.Next (d)) {
			cam_pool_t *cp=d->d()->GetCAMref();
			if(d->d()->Priority()<0 && d->d()->GetCaEnable() && (slot == -1 || slot == cp->slot)) {
#ifdef DEBUG_RESOURCES
				printf("Can Steal CAM on slot %d from %d\n", slot, d->d()->CardIndex()+1);
#endif
				if(force) {
					d->d ()->SetTempDisable (true);
#ifdef DEBUG_RESOURCES
					printf("Stole CAM on slot %d from %d\n", slot, d->d()->CardIndex()+1);
#endif
				}
				return true;
			}
		}
		return false;
}

satellite_list_t *cPluginMcli::TunerFindSatList(const netceiver_info_t *nc_info, const char *SatelliteListName) const
{
	if(SatelliteListName == NULL) {
		return NULL;
	}

	for (int i = 0; i < nc_info->sat_list_num; i++) {
		if (!strcmp (SatelliteListName, nc_info->sat_list[i].Name)) {
//			printf ("found uuid in sat list %d\n", i);
			return nc_info->sat_list + i;
		}
	}
	return NULL;
}

bool cPluginMcli::SatelitePositionLookup(const satellite_list_t *satlist, int pos) const
{
	if(satlist == NULL) {
		return false;
	}
	for(int i=0; i<satlist->sat_num;i ++) {
		satellite_info_t *s=satlist->sat+i;
		switch(s->type){
			case SAT_SRC_LNB:
			case SAT_SRC_UNI:
				if(pos == s->SatPos) {
//					printf("satlist found\n");
					return true;
				}
				break;
			case SAT_SRC_ROTOR:
				if(pos>=s->SatPosMin && pos <=s->SatPosMax) {
//					printf("satlist found\n");
					return true;
				}
				break;
		}
	}
//	printf("satlist not found\n");
	
	return false;
}

bool cPluginMcli::TunerSatelitePositionLookup(tuner_pool_t *tp, int pos) const
{
	if((tp->type != FE_QPSK) && (tp->type != FE_DVBS2)) {
		return true;
	}
	if(pos == NO_SAT_POS) {
		return true;
	}
	nc_lock_list ();
	netceiver_info_list_t *nc_list = nc_get_list ();
	satellite_list_t *satlist=NULL;
	for (int n = 0; n < nc_list->nci_num; n++) {
		netceiver_info_t *nci = nc_list->nci + n;
		int l=strlen(tp->uuid)-5;
		if(strncmp(nci->uuid, tp->uuid, l)) {
			continue;
		}
		satlist=TunerFindSatList(nci, tp->SatListName);
		if(satlist) {
			break;
		}
	}
	bool ret;
	if(satlist == NULL) {
		ret = false;
	} else {	
		ret=SatelitePositionLookup(satlist, pos);
	}
	nc_unlock_list ();
	return ret;
}
tuner_pool_t *cPluginMcli::TunerFindByUUID (const char *uuid)
{
	tuner_pool_t *tp;
	for(int i=0; i<TUNER_POOL_MAX; i++) {
		tp=m_tuner_pool+i;
		if(tp->type != -1 && !strcmp(tp->uuid, uuid)) {
			return tp;
		}
	}
	return NULL;
}

bool cPluginMcli::Ready()
{
	tuner_pool_t *tp;
	for(int i=0; i<TUNER_POOL_MAX; i++) {
		tp=m_tuner_pool+i;
		if(tp->type != -1) 
		    return true;
	}
	return false;
}

int cPluginMcli::TunerCountByType (const fe_type_t type)
{
	int ret=0;
	tuner_pool_t *tp;
	for(int i=0; i<TUNER_POOL_MAX; i++) {
		tp=m_tuner_pool+i;
		if(tp->inuse && tp->type == type) {
			ret++;
		}
	}
	return ret;
}

bool  cPluginMcli::TunerPoolAdd(tuner_info_t *t)
{
	tuner_pool_t *tp;
	for(int i=0; i<TUNER_POOL_MAX; i++) {
		tp=m_tuner_pool+i;
		if(tp->type == -1) {
			tp->type=t->fe_info.type;
			strcpy(tp->uuid, t->uuid);
			strcpy(tp->SatListName, t->SatelliteListName);
			return true;
		}
	}
	return false;
}
bool cPluginMcli::TunerPoolDel(tuner_pool_t *tp)
{
	if(tp->type != -1) {
		tp->type=-1;
		return true;
	}
	return false;
}

tuner_pool_t *cPluginMcli::TunerAvailable(fe_type_t type, int pos, bool lock)
{
	tuner_pool_t *tp;
	if(lock) {
		Lock();
	}
//	printf("TunerAvailable: %d %d\n",type, pos);
	if (TunerCountByType (type) == m_cmd.tuner_type_limit[type]) {
#ifdef DEBUG_RESOURCES
		//printf("Type %d limit (%d) reached\n", type, m_cmd.tuner_type_limit[type]);
#endif		
		if(lock) {
			Unlock();
		}
		return NULL;
	}

	for(int i=0; i<TUNER_POOL_MAX; i++) {
		tp=m_tuner_pool+i;
//		printf("Tuner %d(%p), type %d, inuse %d\n", i, tp, tp->type, tp->inuse);

		if(tp->inuse) {
			continue;
		}
		if(tp->type != type) {
			continue;
		}
		if(TunerSatelitePositionLookup(tp, pos)) {
//			printf("TunerAvailable: %d/%p\n",i,tp);
			if(lock) {
				Unlock();
			}
			return tp;
		}
	}
	if(lock) {
		Unlock();
	}
	return NULL;
}

tuner_pool_t *cPluginMcli::TunerAlloc(fe_type_t type, int pos, bool lock)
{
	tuner_pool_t *tp;
	if(lock) {
		Lock();
	}
	tp=TunerAvailable(type, pos, false);
	if(tp) {
		tp->inuse=true;
#ifdef DEBUG_RESOURCES
		printf("TunerAlloc: %p type %d\n",tp, tp->type);
#endif
		if(lock) {
			Unlock();
		}
		return tp;
	}
		if(lock) {
			Unlock();
		}
	return NULL;
}
bool cPluginMcli::TunerFree(tuner_pool_t *tp, bool lock)
{
	if(lock) {
		Lock();
	}
	if(tp->inuse) {
		tp->inuse=false;
#ifdef DEBUG_RESOURCES
		printf("TunerFree: %p type %d\n",tp, tp->type);
#endif
		if(lock) {
			Unlock();
		}
		return true;
	}
	if(lock) {
		Unlock();
	}
	return false;
}

void cPluginMcli::Action (void)
{
	netceiver_info_list_t *nc_list = nc_get_list ();
//	printf ("Looking for netceivers out there....\n");
	bool channel_switch_ok = false;
#define NOTIFY_CAM_CHANGE 1
#ifdef NOTIFY_CAM_CHANGE
    int cam_stats[CAM_POOL_MAX] = { 0 };
    char menu_strings[CAM_POOL_MAX][MAX_MENU_STR_LEN];
    bool first_run = true;

    for (int i = 0; i < CAM_POOL_MAX; i++)
        menu_strings[i][0] = '\0';
#endif
	
	while (Running ()) {
		Lock ();
		nc_lock_list ();
		time_t now = time (NULL);
		bool tpa = false;
		
		for (int n = 0; n < nc_list->nci_num; n++) {
			netceiver_info_t *nci = nc_list->nci + n;
			if ((now - nci->lastseen) > MCLI_DEVICE_TIMEOUT) {
				if(CAMPoolDel(nci->uuid)) {
					printf  ("mcli: Remove CAMs from NetCeiver %s\n", nci->uuid);
					isyslog ("mcli: Remove CAMs from NetCeiver %s\n", nci->uuid);
				}
			} else {
				int cpa = CAMPoolAdd(nci);
				if(cpa==1) {
					printf ("mcli: Add CAMs from NetCeiver %s -> %d\n", nci->uuid, cpa);
					isyslog ("mcli: Add CAMs from NetCeiver %s -> %d\n", nci->uuid, cpa);
				}
			}

#if NOTIFY_CAM_CHANGE
            if (n == 0) {
                for(int j = 0; j < nci->cam_num && j < CAM_POOL_MAX; j++) {
                    if (nci->cam[j].status != cam_stats[j]) {
                        char buf[64];
                        if (nci->cam[j].status) {
                            if(nci->cam[j].status == 2 && !first_run) {
                                snprintf(buf, 64, tr("Module '%s' ready"), nci->cam[j].menu_string);
                                Skins.QueueMessage(mtInfo, buf);
                            }
                            cam_stats[j] = nci->cam[j].status;
                            strncpy(menu_strings[j], nci->cam[j].menu_string, MAX_MENU_STR_LEN);
                        } else if (nci->cam[j].status == 0) {
                            cam_stats[j] = nci->cam[j].status;
                            if (!first_run) {
                                snprintf(buf, 64, tr("Module '%s' removed"), (char*)menu_strings[j]);
                                Skins.QueueMessage(mtInfo, buf);
                            }
                            menu_strings[j][0] = '\0';
                        }
                    }
                }
                first_run = false;
            }
#endif

			for (int i = 0; i < nci->tuner_num; i++) {
				tuner_pool_t *t = TunerFindByUUID (nci->tuner[i].uuid);
				if (((now - nci->lastseen) > MCLI_DEVICE_TIMEOUT) || (nci->tuner[i].preference < 0) || !strlen (nci->tuner[i].uuid)) {
					if (t) {
						int pos=TunerPoolDel(t);
						printf  ("mcli: Remove Tuner %s [%s] @ %d\n", nci->tuner[i].fe_info.name, nci->tuner[i].uuid, pos);
						isyslog ("mcli: Remove Tuner %s [%s] @ %d", nci->tuner[i].fe_info.name, nci->tuner[i].uuid, pos);
					}
					continue;
				}
				if (!t) {
					tpa=TunerPoolAdd(nci->tuner+i);
					printf ("mcli: Add Tuner: %s [%s], Type %d @ %d\n", nci->tuner[i].fe_info.name, nci->tuner[i].uuid, nci->tuner[i].fe_info.type, tpa);
					isyslog ("mcli: Add Tuner: %s [%s], Type %d @ %d", nci->tuner[i].fe_info.name, nci->tuner[i].uuid, nci->tuner[i].fe_info.type, tpa);
				}
			}
		}
		nc_unlock_list ();
		Unlock ();
		if (tpa) {
			if(!m_devs.Count()) {
				for(int i=0; i < MCLI_MAX_DEVICES; i++) {
					cMcliDevice *m = NULL;
					cPluginManager::CallAllServices ("OnNewMcliDevice-" MCLI_DEVICE_VERSION, &m);
					if(!m) {
						m = new cMcliDevice;
					}
					if(m) {
						m->SetMcliRef (this);
						m->SetEnable ();
						cMcliDeviceObject *d = new cMcliDeviceObject (m);
						m_devs.Add (d);
					}
				}
			}
//TB: reelvdr itself tunes if the first tuner appears, don't do it twice
#ifndef REELVDR
			if (!channel_switch_ok) {	// the first tuner that was found, so make VDR retune to the channel it wants...
				cChannel *ch = Channels.GetByNumber (cDevice::CurrentChannel ());
                                #if defined (APIVERSNUM) && (APIVERSNUM >= 20301)
                                         LOCK_CHANNELS_READ;
                                         const cChannel *ch = Channels->GetByNumber(1,0);
                                #else
                                         cChannel *ch = Channels.GetByNumber (cDevice::CurrentChannel ());
                                #endif				
				if (ch) {
					channel_switch_ok = cDevice::PrimaryDevice ()->SwitchChannel (ch, true);
				}
			}
#endif
		} else {
			channel_switch_ok = 0;
		}

#ifdef TEMP_DISABLE_DEVICE
		TempDisableDevices();
#endif
		usleep (250 * 1000);
	}
}
void cPluginMcli::TempDisableDevices(bool now)
{
		for (cMcliDeviceObject * d = m_devs.First (); d; d = m_devs.Next (d)) {
			d->d ()->SetTempDisable (now);
		}

}
bool cPluginMcli::Initialize (void)
{
	return InitMcli ();
}


bool cPluginMcli::Start (void)
{
//	printf ("cPluginMcli::Start\n");
	isyslog("mcli v"MCLI_PLUGIN_VERSION" started");
#ifdef REELVDR
    if (access("/dev/dvb/adapter0", F_OK) != 0) //TB: this line allows the client to be used with usb-sticks without conflicts
#endif
	cThread::Start ();
	// Start any background activities the plugin shall perform.
	return true;
}

void cPluginMcli::Stop (void)
{
//	printf ("cPluginMcli::Stop\n");
	cThread::Cancel (0);
	for (cMcliDeviceObject * d = m_devs.First (); d; d = m_devs.Next (d)) {
		d->d ()->SetEnable (false);
	}
	// Stop any background activities the plugin is performing.
}

void cPluginMcli::Housekeeping (void)
{
	// printf ("cPluginMcli::Housekeeping\n");
}

void cPluginMcli::MainThreadHook (void)
{
//      printf("cPluginMcli::MainThreadHook\n");
	if (reconf) {
		reconfigure ();
		reconf = 0;
	}
#if 0
	cOsdObject *MyMenu = AltMenuAction ();
	if (MyMenu) {		// is there any cam-menu waiting?
		if (cControl::Control ()) {
			cControl::Control ()->Hide ();
		}
		MyMenu->Show ();
	}
#endif
}

cString cPluginMcli::Active (void)
{
//	printf ("cPluginMcli::Active\n");
	// Return a message string if shutdown should be postponed
	return NULL;
}

time_t cPluginMcli::WakeupTime (void)
{
//	printf ("cPluginMcli::WakeupTime\n");
	// Return custom wakeup time for shutdown script
	return 0;
}

void cPluginMcli::reconfigure (void)
{
	Lock();
	for (cMcliDeviceObject * d = m_devs.First (); d;) {
		cMcliDeviceObject *next = m_devs.Next (d);
		d->d ()->SetEnable (false);
		d->d ()->ExitMcli ();
		d = next;
	}
	ExitMcli ();
	memset (m_tuner_pool, 0, sizeof(tuner_pool_t)*TUNER_POOL_MAX);
	for(int i=0; i<TUNER_POOL_MAX; i++) {
		m_tuner_pool[i].type = -1;
	}
	for(int i=0; i<CAM_POOL_MAX; i++) {
		m_cam_pool[i].max = -1;
	}
	InitMcli ();
	for (cMcliDeviceObject * d = m_devs.First (); d; d = m_devs.Next (d)) {
		d->d ()->InitMcli ();
	}
	Unlock();
	usleep(3*1000*1000);
	for (cMcliDeviceObject * d = m_devs.First (); d; d = m_devs.Next (d)) {
		d->d ()->SetEnable (true);
	}
}

cOsdObject *cPluginMcli::MainMenuAction (void)
{
//	printf ("cPluginMcli::MainMenuAction\n");
	// Perform the action when selected from the main VDR menu.
	return new cCamMenu (&m_cmd);
}


cMenuSetupPage *cPluginMcli::SetupMenu (void)
{
//	printf ("cPluginMcli::SetupMenu\n");
	// Return a setup menu in case the plugin supports one.
	return new cMenuSetupMcli (&m_cmd);
}

bool cPluginMcli::SetupParse (const char *Name, const char *Value)
{
//      printf ("cPluginMcli::SetupParse\n");
	if (!strcasecmp (Name, "DVB-C") && m_cmd.tuner_type_limit[FE_QAM] == MCLI_MAX_DEVICES)
		m_cmd.tuner_type_limit[FE_QAM] = atoi (Value);
	else if (!strcasecmp (Name, "DVB-T") && m_cmd.tuner_type_limit[FE_OFDM] == MCLI_MAX_DEVICES)
		m_cmd.tuner_type_limit[FE_OFDM] = atoi (Value);
	else if (!strcasecmp (Name, "DVB-S") && m_cmd.tuner_type_limit[FE_QPSK] == MCLI_MAX_DEVICES)
		m_cmd.tuner_type_limit[FE_QPSK] = atoi (Value);
	else if (!strcasecmp (Name, "DVB-S2") && m_cmd.tuner_type_limit[FE_DVBS2] == MCLI_MAX_DEVICES)
		m_cmd.tuner_type_limit[FE_DVBS2] = atoi (Value);
	else
		return false;
	return true;
}

bool cPluginMcli::Service (const char *Id, void *Data)
{
	//printf ("cPluginMcli::Service: \"%s\"\n", Id);
	mclituner_info_t *infos = (mclituner_info_t *) Data;

	if (Id && strcmp (Id, "GetTunerInfo") == 0) {
		int j=0;
		time_t now = time (NULL);
		netceiver_info_list_t *nc_list = nc_get_list ();
		nc_lock_list ();
		for (int n = 0; n < nc_list->nci_num; n++) {
			netceiver_info_t *nci = nc_list->nci + n;
			if ((now - nci->lastseen) > MCLI_DEVICE_TIMEOUT) {
				continue;
			}
			for (int i = 0; i < nci->tuner_num && j < MAX_TUNERS_IN_MENU; i++) {
				strcpy (infos->name[j], nci->tuner[i].fe_info.name);
				infos->type[j] = nci->tuner[i].fe_info.type;
				infos->preference[j++] = nci->tuner[i].preference;
				//printf("Tuner: %s\n", nci->tuner[i].fe_info.name);
			}
		}
		nc_unlock_list ();
		return true;
	} else if (Id && strcmp (Id, "Reinit") == 0) {
		if (Data && strlen ((char *) Data) && (strncmp ((char *) Data, "eth", 3) || strncmp ((char *) Data, "br", 2))) {
			strncpy (m_cmd.iface, (char *) Data, IFNAMSIZ - 1);
		}
		reconfigure ();
		return true;
	}
	// Handle custom service requests from other plugins
	return false;
}

const char **cPluginMcli::SVDRPHelpPages (void)
{
//	printf ("cPluginMcli::SVDRPHelpPages\n");
	// Return help text for SVDRP commands this plugin implements
	static const char *HelpPages[] = {
		"REINIT [dev]\n" "    Reinitalize the plugin on a certain network device - e.g.: plug mcli REINIT eth0",
		NULL
	};
	return HelpPages;
}

cString cPluginMcli::SVDRPCommand (const char *Command, const char *Option, int &ReplyCode)
{
//	printf ("cPluginMcli::SVDRPCommand\n");
	// Process SVDRP commands this plugin implements

	if (strcasecmp (Command, "REINIT") == 0) {
		if (Option && (strncmp (Option, "eth", 3) || strncmp (Option, "br", 2))) {
			strncpy (m_cmd.iface, (char *) Option, IFNAMSIZ - 1);
		}
		reconfigure ();
		return cString ("Mcli-plugin: reconfiguring...");
	}

	return NULL;
}

VDRPLUGINCREATOR (cPluginMcli);	// Don't touch this!
