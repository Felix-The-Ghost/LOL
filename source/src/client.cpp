// client.cpp, mostly network related client game code

#include "cube.h"
#include "bot/bot.h"

ENetHost *clienthost = NULL;
ENetPeer *curpeer = NULL, *connpeer = NULL;
int connmillis = 0, connattempts = 0, discmillis = 0;
bool c2sinit = false;       // whether we need to tell the other clients our stats

int getclientnum() { return player1 ? player1->clientnum : -1; }

bool multiplayer()
{
    // check not correct on listen server?
    if(curpeer) conoutf("operation not available in multiplayer");
    return curpeer!=NULL;
}

bool allowedittoggle()
{
    bool allow = !curpeer || gamemode==1;
    if(!allow) conoutf("editing in multiplayer requires coopedit mode (1)");
    return allow; 
}

void setrate(int rate)
{
   if(!curpeer) return;
   enet_host_bandwidth_limit(clienthost, rate, rate);
}

VARF(rate, 0, 0, 25000, setrate(rate));

void throttle();

VARF(throttle_interval, 0, 5, 30, throttle());
VARF(throttle_accel,    0, 2, 32, throttle());
VARF(throttle_decel,    0, 2, 32, throttle());

void throttle()
{
    if(!curpeer) return;
    ASSERT(ENET_PEER_PACKET_THROTTLE_SCALE==32);
    enet_peer_throttle_configure(curpeer, throttle_interval*1000, throttle_accel, throttle_decel);
}

void newname(char *name) 
{ 
    if(name[0])
    {
        c2sinit = false; 
        s_strncpy(player1->name, name, MAXNAMELEN+1); 
    }
    else conoutf("your name is: %s", player1->name);
}

int smallerteam()
{
	int teamsize[2] = {0, 0};
	loopv(players) if(players[i]) teamsize[team_int(players[i]->team)]++;
	teamsize[team_int(player1->team)]++;
	if(teamsize[0] == teamsize[1]) return -1;
	return teamsize[0] < teamsize[1] ? 0 : 1;
}

void changeteam(int team) // force team and respawn
{
	c2sinit = false;
	if(m_ctf) tryflagdrop(NULL);
	s_strncpy(player1->team, team_string(team), MAXTEAMLEN+1);
	deathstate(player1);
}

void newteam(char *name)
{
    if(name[0])
    {
        if(m_teammode)
		{
			if(!strcmp(name, player1->team)) return; // same team
			if(!team_valid(name)) { conoutf("\f3\"%s\" is not a valid team name (try CLA or RVSF)", name); return; }

			bool checkteamsize =  autoteambalance && players.length() >= 1 && !m_botmode;
			int freeteam = smallerteam();

			if(team_valid(name))
			{
				int team = team_int(name);
				if(checkteamsize && team != freeteam)
				{ 
					conoutf("\f3the %s team is already full", name);
					return;
				}
				changeteam(team);
			}
			else changeteam(checkteamsize ? (uint)freeteam : rnd(2)); // random assignement
		}
    }
    else conoutf("your team is: %s", player1->team);
}

void newskin(int skin) { player1->nextskin = skin; }

COMMANDN(team, newteam, ARG_1STR);
COMMANDN(name, newname, ARG_1STR);
COMMANDN(skin, newskin, ARG_1INT);

string clientpassword = "";

void abortconnect()
{
    if(!connpeer) return;
    if(connpeer->state!=ENET_PEER_STATE_DISCONNECTED) enet_peer_reset(connpeer);
    connpeer = NULL;
    if(curpeer) return;
    enet_host_destroy(clienthost);
    clienthost = NULL;
}

void connects(char *servername, char *password)
{   
    if(connpeer)
    {
        conoutf("aborting connection attempt");
        abortconnect();
    }

    s_strcpy(clientpassword, password ? password : "");

    ENetAddress address;
    address.port = CUBE_SERVER_PORT;

    if(servername)
    {
        addserver(servername);
        conoutf("attempting to connect to %s", servername);
        if(!resolverwait(servername, &address))
        {
            conoutf("\f3could not resolve server %s", servername);
            return;
        }
    }
    else
    {
        conoutf("attempting to connect over LAN");
        address.host = ENET_HOST_BROADCAST;
    }

    if(!clienthost) clienthost = enet_host_create(NULL, 2, rate, rate);

    if(clienthost)
    {
        connpeer = enet_host_connect(clienthost, &address, 3); 
        enet_host_flush(clienthost);
        connmillis = lastmillis;
        connattempts = 0;
    }
    else conoutf("\f3could not connect to server");
}

void lanconnect()
{
    connects(0);
}  

void disconnect(int onlyclean, int async)
{
    bool cleanup = onlyclean!=0;
    if(curpeer)
    {
        if(!discmillis)
        {
            enet_peer_disconnect(curpeer, DISC_NONE);
            enet_host_flush(clienthost);
            discmillis = lastmillis;
        }
        if(curpeer->state!=ENET_PEER_STATE_DISCONNECTED)
        {
            if(async) return;
            enet_peer_reset(curpeer);
        }
        curpeer = NULL;
        discmillis = 0;
        conoutf("disconnected");
        cleanup = true;
    }
    if(cleanup)
    {
        stop();
        c2sinit = false;
        player1->clientnum = -1;
        player1->lifesequence = 0;
        player1->ismaster = false;
        if(m_botmode) BotManager.EndMap();
        loopv(players) zapplayer(players[i]);
        localdisconnect();
    }
    if(!connpeer && clienthost)
    {
        enet_host_destroy(clienthost);
        clienthost = NULL;
    }
    if(!onlyclean) localconnect();
}

void trydisconnect()
{
    if(connpeer)
    {
        conoutf("aborting connection attempt");
        abortconnect();
        return;
    }
    if(!curpeer)
    {
        conoutf("not connected");
        return;
    }
    conoutf("attempting to disconnect...");
    disconnect(0, !discmillis);
}

void toserver(char *text) 
{ 
    bool toteam = text && text[0] == '%';
    if(toteam) text++;
    conoutf("%s:\f%d %s", player1->name, toteam ? 1 : 0, text);
    addmsg(toteam ? SV_TEAMTEXT : SV_TEXT, "rs", text);
}

void echo(char *text) { conoutf("%s", text); }

COMMAND(echo, ARG_VARI);
COMMANDN(say, toserver, ARG_VARI);
COMMANDN(connect, connects, ARG_2STR);
COMMAND(lanconnect, ARG_NONE);
COMMANDN(disconnect, trydisconnect, ARG_NONE);

// collect c2s messages conveniently

vector<uchar> messages;

void addmsg(int type, const char *fmt, ...)
{
    if(demoplayback) return;
    static uchar buf[MAXTRANS];
    ucharbuf p(buf, MAXTRANS);
    putint(p, type);
    int numi = 1, nums = 0;
    bool reliable = false;
    if(fmt)
    {
        va_list args;
        va_start(args, fmt);
        while(*fmt) switch(*fmt++)
        {
            case 'r': reliable = true; break;
            case 'i':
            {
                int n = isdigit(*fmt) ? *fmt++-'0' : 1;
                loopi(n) putint(p, va_arg(args, int));
                numi += n;
                break;
            }
            case 's': sendstring(va_arg(args, const char *), p); nums++; break;
        }
        va_end(args);
    }
    int num = nums?0:numi;
    if(num!=msgsizelookup(type)) { s_sprintfd(s)("inconsistant msg size for %d (%d != %d)", type, num, msgsizelookup(type)); fatal(s); }
    int len = p.length();
    messages.add(len&0xFF);
    messages.add((len>>8)|(reliable ? 0x80 : 0));
    loopi(len) messages.add(buf[i]);
}

int lastupdate = 0, lastping = 0;
bool senditemstoserver = false;     // after a map change, since server doesn't have map data

bool netmapstart() { senditemstoserver = true; return curpeer!=NULL; }

void initclientnet()
{
    newname("unnamed");
    changeteam(rnd(2));
}

void sendpackettoserv(int chan, ENetPacket *packet)
{
    if(curpeer) enet_peer_send(curpeer, chan, packet);
    else localclienttoserver(chan, packet);
}

void c2skeepalive()
{
    if(clienthost) enet_host_service(clienthost, NULL, 0);
}

extern string masterpwd;

void c2sinfo(playerent *d)                  // send update to the server
{
    if(d->clientnum<0) return;              // we haven't had a welcome message from the server yet
    if(lastmillis-lastupdate<40) return;    // don't update faster than 25fps
    ENetPacket *packet = enet_packet_create(NULL, 100, 0);
    ucharbuf q(packet->data, packet->dataLength);

    putint(q, SV_POS);
    putint(q, d->clientnum);
    putuint(q, (int)(d->o.x*DMF));       // quantize coordinates to 1/16th of a cube, between 1 and 3 bytes
    putuint(q, (int)(d->o.y*DMF));
    putuint(q, (int)(d->o.z*DMF));
    putuint(q, (int)(d->yaw*DAF));
    putint(q, (int)(d->pitch*DAF));
    putint(q, (int)(d->roll*DAF));
    putint(q, (int)(d->vel.x*DVF));     // quantize to 1/100, almost always 1 byte
    putint(q, (int)(d->vel.y*DVF));
    putint(q, (int)(d->vel.z*DVF));
    // pack rest in 1 int: strafe:2, move:2, onfloor:1, state:3, onladder: 1
    putint(q, (d->strafe&3) | ((d->move&3)<<2) | (((int)d->onfloor)<<4) | ((editmode ? CS_EDITING : d->state)<<5) | (((int)d->onladder)<<8) );

    enet_packet_resize(packet, q.length());
    incomingdemodata(0, q.buf, q.length(), true);
    sendpackettoserv(0, packet);

    bool serveriteminitdone = false;
    if(gun_changed || senditemstoserver || !c2sinit || messages.length() || lastmillis-lastping>250)
    {
        packet = enet_packet_create (NULL, MAXTRANS, 0);
        ucharbuf p(packet->data, packet->dataLength);
    
        if(clientpassword[0])
        {
            putint(p, SV_PWD);
            sendstring(clientpassword, p);
            clientpassword[0] = 0;
        }
        if(!c2sinit)    // tell other clients who I am
        {
            packet->flags = ENET_PACKET_FLAG_RELIABLE;
            c2sinit = true;
            putint(p, SV_INITC2S);
            sendstring(player1->name, p);
            sendstring(player1->team, p);
            putint(p, player1->skin);
            putint(p, player1->lifesequence);
        }
        if(gun_changed)
        {
            packet->flags = ENET_PACKET_FLAG_RELIABLE;
            putint(p, SV_WEAPCHANGE);
            putint(p, player1->gunselect);
            gun_changed = false;       
        }
        if(senditemstoserver)
        {
            packet->flags = ENET_PACKET_FLAG_RELIABLE;
            putint(p, SV_ITEMLIST);
            if(!m_noitems) putitems(p);
            putint(p, -1);
            senditemstoserver = false;
            serveriteminitdone = true;
        }
        int i = 0;
        while(i < messages.length()) // send messages collected during the previous frames
        {
            int len = messages[i] | ((messages[i+1]&0x7F)<<8);
            if(p.remaining() < len) break;
            if(messages[i+1]&0x80) packet->flags = ENET_PACKET_FLAG_RELIABLE;
            p.put(&messages[i+2], len);
            i += 2 + len;
        }
        messages.remove(0, i);
        if(lastmillis-lastping>250)
        {
            putint(p, SV_PING);
            putint(p, lastmillis);
            lastping = lastmillis;
        }
        if(!p.length()) enet_packet_destroy(packet);
        else
        {
            enet_packet_resize(packet, p.length());
            incomingdemodata(42, p.buf, p.length());
            sendpackettoserv(1, packet);
        }
    }
    if(clienthost) enet_host_flush(clienthost);
    lastupdate = lastmillis;
    if(serveriteminitdone) loadgamerest();  // hack
}

void gets2c()           // get updates from the server
{
    ENetEvent event;
    if(!clienthost) return;
    if(connpeer && lastmillis/3000 > connmillis/3000)
    {
        conoutf("attempting to connect...");
        connmillis = lastmillis;
        ++connattempts;
        if(connattempts > 3)
        {
            conoutf("\f3could not connect to server");
            abortconnect();
            return;
        }
    }
    while(clienthost!=NULL && enet_host_service(clienthost, &event, 0)>0)
    switch(event.type)
    {
        case ENET_EVENT_TYPE_CONNECT:
            disconnect(1);
            curpeer = connpeer;
            connpeer = NULL;
            conoutf("connected to server");
            throttle();
            if(rate) setrate(rate);
            if(editmode) toggleedit();
            break;
         
        case ENET_EVENT_TYPE_RECEIVE:
            if(discmillis) conoutf("attempting to disconnect...");
            else localservertoclient(event.channelID, event.packet->data, (int)event.packet->dataLength);
            enet_packet_destroy(event.packet);
            break;

        case ENET_EVENT_TYPE_DISCONNECT:
            extern char *disc_reasons[];
            if(event.data>=DISC_NUM) event.data = DISC_NONE;
            if(event.peer==connpeer)
            {
                conoutf("\f3could not connect to server");
                abortconnect();
            }
            else
            {
                if(!discmillis || event.data) conoutf("\f3server network error, disconnecting (%s) ...", disc_reasons[event.data]);
                disconnect();
            }
            return;

        default:
            break;
    }
}
