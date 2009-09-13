/***************************************************************************
 *   Copyright (C) 2009;   Author:  Tobias Bratfisch                       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *
 ***************************************************************************/


#include "cam_menu.h"
#include "filter.h"
#include "device.h"

#include <string.h>

cCamMenu::cCamMenu (cmdline_t * cmd):cOsdMenu (trVDR ("Common Interface"), 18)
{
	m_cmd = cmd;
	inCamMenu = false;
	inMMIBroadcastMenu = false;
	inputRequested = eInputNone;
	end = false;
	currentSelected = -1;
	pinCounter = 0;
	alreadyReceived = false;
	mmi_session = -1;

	for (int i = 0; i < MAX_CAMS_IN_MENU; i++) {
		cam_list[i].slot = -1;
        cam_list[i].info[0] = '\0';
    }

    SetHelp(trVDR("Reset"), NULL, NULL, NULL);

	SetNeedsFastResponse (true);

	// Find all operational CAMs.
	CamFind (cam_list);
}

cCamMenu::cCamMenu (cmdline_t * cmd, mmi_info_t * mmi_info):cOsdMenu (trVDR ("Common Interface"), 18)
{
	m_cmd = cmd;
	inCamMenu = false;
	inMMIBroadcastMenu = false;
	inputRequested = eInputNone;
	end = false;
	currentSelected = -1;
	pinCounter = 0;
	alreadyReceived = false;
	mmi_session = -1;

	for (int i = 0; i < MAX_CAMS_IN_MENU; i++) {
		cam_list[i].slot = -1;
        cam_list[i].info[0] = '\0';
    }

	SetNeedsFastResponse (true);

    SetHelp(trVDR("Reset"), NULL, NULL, NULL);

	mmi_session = CamMenuOpen (mmi_info);
}


cCamMenu::~cCamMenu ()
{
	CamMenuClose (mmi_session);
}

void cCamMenu::OpenCamMenu ()
{
	bool timeout = true;

	unsigned int nrInCamList = currentSelected - ((int) currentSelected / 5) * 3 - 3;	// minus the empty rows

	if (cam_list[nrInCamList].slot == -1)	// just a sanity check
		return;

	Clear ();
    Skins.Message(mtWarning, trVDR("Opening CAM menu..."));

	mmi_session = CamMenuOpen (&cam_list[nrInCamList]);
	char buf2[MMI_TEXT_LENGTH * 2];
	printf ("mmi_session: %d\n", mmi_session);
	if (mmi_session > 0) {
		inCamMenu = true;
		time_t t = time (NULL);
		while ((time (NULL) - t) < CAMMENU_TIMEOUT) {
			// receive the CAM MENU
			if (CamMenuReceive (mmi_session, buf, MMI_TEXT_LENGTH) > 0) {
				cCharSetConv conv = cCharSetConv ("ISO-8859-1", "UTF-8");
				conv.Convert (buf, buf2, MMI_TEXT_LENGTH * 2);
				char *saveptr = NULL;
				char *ret = strtok_r (buf2, "\n", &saveptr);
				if (ret) {
					Add (new cOsdItem (ret));
					while (ret != NULL) {
						ret = strtok_r (NULL, "\n", &saveptr);
						if (ret)
							Add (new cOsdItem (ret));
					}
				}
				timeout = false;
				break;
			}
		}
	}
	if (timeout) {
		printf ("%s: Error\n", __PRETTY_FUNCTION__);
		Add (new cOsdItem (trVDR ("Error")));
	}
	Display ();
}

void cCamMenu::Receive ()
{
	bool timeout = true;
	if (mmi_session > 0) {
		char buf2[MMI_TEXT_LENGTH * 2];
		time_t t = time (NULL);
		while ((time (NULL) - t) < CAMMENU_TIMEOUT) {
			// receive the CAM MENU
			if (alreadyReceived || CamMenuReceive (mmi_session, buf, MMI_TEXT_LENGTH) > 0) {
				Clear ();
				alreadyReceived = false;
				printf ("MMI: \"%s\"\n", buf);
				if (!strncmp (buf, "blind = 0", 9))
					inputRequested = eInputNotBlind;
				else if (!strncmp (buf, "blind = 1", 9))
					inputRequested = eInputBlind;
				cCharSetConv conv = cCharSetConv ("ISO-8859-1", "UTF-8");
				conv.Convert (inputRequested ? buf + 28 : buf, buf2, MMI_TEXT_LENGTH * 2);
				printf ("MMI-UTF8: \"%s\"\n", buf2);
				if (!strcmp (buf, "end")) {
					  /** The Alphacrypt returns "end" when pressing "Back" in it's main menu */
					end = true;
					return;
				}
				char *saveptr = NULL;
				char *ret = strtok_r (buf2, "\n", &saveptr);
				if (ret) {
					Add (new cOsdItem (ret));
					while (ret != NULL) {
						ret = strtok_r (NULL, "\n", &saveptr);
						if (ret)
							Add (new cOsdItem (ret));
					}
				}
				timeout = false;
				break;
			}
		}
	}
	if (timeout) {
		printf ("%s: mmi_session: %i\n", __PRETTY_FUNCTION__, mmi_session);
		Add (new cOsdItem (trVDR ("Error")));
	}
	Display ();
}

int cCamMenu::CamFind (cam_list_t * cam_list)
{
	Clear ();
	int n, cnt = 0, i;
	netceiver_info_list_t *nc_list = nc_get_list ();
	printf ("Looking for netceivers out there....\n");
	nc_lock_list ();
	for (n = 0; n < nc_list->nci_num; n++) {
		netceiver_info_t *nci = nc_list->nci + n;
		printf ("\nFound NetCeiver: %s \n", nci->uuid);
		char buf[128];
		Add (new cOsdItem ("Netceiver", osUnknown, false));
		snprintf (buf, 128, "    %s: %s", "ID", nci->uuid);
		Add (new cOsdItem (buf, osUnknown, false));
		Add (new cOsdItem ("", osUnknown, false));
		printf ("    CAMS [%d]: \n", nci->cam_num);
		for (i = nci->cam_num - 1; i >= 0 /*nci->cam_num */ ; i--) {
			switch (nci->cam[i].status) {
			case 2:	//DVBCA_CAMSTATE_READY:
				snprintf (buf, 128, "   %s:\t%s", nci->cam[i].slot == 0 ? trVDR ("lower slot") : trVDR ("upper slot"), nci->cam[i].menu_string);
				Add (new cOsdItem (buf));
				printf ("    %i.CAM - Slot %i - %s\n", i + 1, nci->cam[i].slot, nci->cam[i].menu_string);
				if (cnt < MAX_CAMS_IN_MENU) {
					cam_list[cnt].slot = i;
					strcpy (cam_list[cnt].uuid, nci->uuid);
					strcpy (cam_list[cnt].info, nci->cam[i].menu_string);
				}
				break;
			default:
				cam_list[cnt].slot = -1;
				int len = strlen (nci->cam[i].menu_string);
				printf ("%s: Error\n", __PRETTY_FUNCTION__);
				snprintf (buf, 128, "   %s:\t%s", nci->cam[i].slot == 0 ? trVDR ("lower slot") : trVDR ("upper slot"), len == 0 ? trVDR ("Error") : nci->cam[i].menu_string);
				Add (new cOsdItem (buf));
			}
			cnt++;
		}
	}
	nc_unlock_list ();
	Display ();
	return cnt;
}

int cCamMenu::CamMenuOpen (cam_list_t * cam)
{
	printf ("Opening CAM Menu at NetCeiver %s Slot %d Current: %i\n", cam->uuid, cam->slot, currentSelected);

	int mmi_session = mmi_open_menu_session (cam->uuid, m_cmd->iface, 0, cam->slot);
	if (mmi_session > 0) {
		sleep (1);
		CamMenuSend (mmi_session, (char *) "00000000000000\n");
	}
	return mmi_session;
}

int cCamMenu::CamMenuOpen (mmi_info_t * mmi_info)
{
	printf ("Opening CAM Menu at NetCeiver %s Slot %d\n", mmi_info->uuid, mmi_info->slot);

	char buf[MMI_TEXT_LENGTH * 2];

	inMMIBroadcastMenu = true;
	inCamMenu = true;
	cCharSetConv conv = cCharSetConv ("ISO-8859-1", "UTF-8");
	conv.Convert (mmi_info->mmi_text, buf, MMI_TEXT_LENGTH * 2);
	printf ("MMI-UTF8: \"%s\"\n", buf);
	char *saveptr = NULL;
	char *ret = strtok_r (buf, "\n", &saveptr);
	if (ret) {
		Add (new cOsdItem (ret));
		while (ret != NULL) {
			ret = strtok_r (NULL, "\n", &saveptr);
			if (ret)
				Add (new cOsdItem (ret));
		}
	}

	int mmi_session = mmi_open_menu_session (mmi_info->uuid, m_cmd->iface, 0, mmi_info->slot);

	printf ("CamMenuOpen: mmi_session: %i\n", mmi_session);
	return mmi_session;
}

int cCamMenu::CamMenuSend (int mmi_session, const char *c)
{
	return mmi_send_menu_answer (mmi_session, (char *) c, strlen (c));
}

int cCamMenu::CamMenuReceive (int mmi_session, char *buf, int bufsize)
{
	return mmi_get_menu_text (mmi_session, buf, bufsize, 50000);
}

void cCamMenu::CamMenuClose (int mmi_session)
{
	close (mmi_session);
}

int cCamMenu::CamPollText (mmi_info_t * text)
{
	return mmi_poll_for_menu_text (m_cam_mmi, text, 10);
}

eOSState cCamMenu::ProcessKey (eKeys Key)
{
	eOSState ret = cOsdMenu::ProcessKey (Key);

	currentSelected = Current ();

	if (end) {
		end = false;
		inCamMenu = false;
		if (inMMIBroadcastMenu)
			return osEnd;
		CamMenuClose (mmi_session);
		CamFind (cam_list);
		return osContinue;
	}
	switch (Key) {
#if 0
    case kUp:
    case kDown:
        {
      	unsigned int nrInCamList = currentSelected - ((int) currentSelected / 5) * 3 - 3;	// minus the empty rows
        if(strlen(cam_list[nrInCamList].info))
            SetHelp(trVDR("Reset"), NULL, NULL, NULL);
        else
            SetHelp(NULL, NULL, NULL, NULL);
        break;
        }
#endif
    case kRed:
        {
      	unsigned int nrInCamList = currentSelected - ((int) currentSelected / 5) * 3 - 3;	// minus the empty rows
        if(cam_list[nrInCamList].slot != -1 && strlen(cam_list[nrInCamList].info))
            mmi_cam_reset(cam_list[nrInCamList].uuid, m_cmd->iface, 0, cam_list[nrInCamList].slot);
        break;
        }
	case kOk:
		SetStatus ("");
		pinCounter = 0;	// reset pin
		if (inCamMenu && inputRequested) {
			inputRequested = eInputNone;	// input was sent
			printf ("Sending pin: \"%s\"\n", pin);
			CamMenuSend (mmi_session, pin);
			Receive ();
		} else if (inMMIBroadcastMenu) {
			return osEnd;
		} else if (inCamMenu) {
			printf ("Sending: \"%s\"\n", Get (Current ())->Text ());
            if (strcmp(Get ( Current ())->Text(), trVDR("Error"))) // never send Error...
			    CamMenuSend (mmi_session, Get (Current ())->Text ());
			Receive ();
		} else
			OpenCamMenu ();
		break;
	case kBack:
		pinCounter = 0;	// reset pin
		if (!inCamMenu)
			return osBack;
		inCamMenu = false;
		SetStatus ("");
		CamMenuClose (mmi_session);
		CamFind (cam_list);
		return osContinue;
	case k0...k9:
		if (inputRequested) {
			pin[pinCounter++] = 48 + Key - k0;
			pin[pinCounter] = '\0';
			printf ("key: %c, pin: \"%s\"\n", 48 + Key - k0, pin);
			char buf[16];
			int i;
			for (i = 0; i < pinCounter; i++)
				(inputRequested == eInputBlind) ? buf[i] = '*' : buf[i] = pin[i];
			buf[i] = '\0';
			SetStatus (buf);
		} else {
			pinCounter = 0;	// reset pin
			SetStatus ("");
			for (int i = 0; Get (i); i++) {
				const char *txt = Get (i)->Text ();
				if (txt[0] == 48 + Key - k0 && txt[1] == '.') {
					CamMenuSend (mmi_session, txt);
					Receive ();
				}
			}
		}
		break;
	default:
		int bla = 0;
		if (mmi_session > 0) {
			bla = CamMenuReceive (mmi_session, buf, MMI_TEXT_LENGTH);
			if (bla > 0) {
				alreadyReceived = true;
				printf ("bla: %i\n", bla);
				Receive ();
			}
		}
		break;
	}
	return ret;
}
