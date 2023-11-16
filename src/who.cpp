/************************************************************************
| $Id: who.cpp,v 1.60 2014/07/04 22:00:04 jhhudso Exp $
| who.C
| Commands for who, maybe? :P
*/
#include <cstring>

#include "connect.h"
#include "utility.h"
#include "character.h"
#include "mobile.h"
#include "terminal.h"
#include "player.h"
#include "levels.h"
#include "clan.h"
#include "room.h"
#include "interp.h"
#include "handler.h"
#include "db.h"
#include "returnvals.h"
#include "const.h"

// TODO - Figure out the weird bug for why when I do "who <class>" a random player
//        from another class will pop up who name is DC::NOWHERE near matching.

clan_data *get_clan(Character *);

// #define GWHOBUFFERSIZE   (MAX_STRING_LENGTH*2)
// char gWhoBuffer[GWHOBUFFERSIZE];

// We now use a allocated pointer for the who buffer stuff.  It stays allocated, so
// we're not repeatedly allocing it, and it grows as needed to fit all the data (like a CString)
// That way we never have to worry about having a bunch of players on, and overflowing it.
// -pir 2/20/01
char *gWhoBuffer = nullptr;
int32_t gWhoBufferCurSize = 0;
int32_t gWhoBufferMaxSize = 0;

void add_to_who(char *strAdd)
{
  int32_t strLength = 0;

  if (!strAdd)
    return;
  if (!(strLength = strlen(strAdd)))
    return;

  if ((strLength + gWhoBufferCurSize) >= gWhoBufferMaxSize)
  {                                         // expand the buffer
    gWhoBufferMaxSize += (strLength + 500); // expand by the size + 500
#ifdef LEAK_CHECK
    gWhoBuffer = (char *)realloc(gWhoBuffer, gWhoBufferMaxSize);
    if (!gWhoBuffer)
    {
      fprintf(stderr, "Unable to realloc in who.C add_to_who");
      abort();
    }
#else
    gWhoBuffer = (char *)dc_realloc(gWhoBuffer, gWhoBufferMaxSize);
#endif
  }

  // guaranteed to work, since we just allocated enough for it + 500
  strcat(gWhoBuffer, strAdd);
  gWhoBufferCurSize += strLength; // update current data size
}

void clear_who_buffer()
{
  if (gWhoBuffer)
    *gWhoBuffer = '\0';  // kill the string
  gWhoBufferCurSize = 0; // update the size
}

int do_whogroup(Character *ch, char *argument, int cmd)
{

  Connection *d;
  Character *k, *i;
  follow_type *f;
  char target[MAX_INPUT_LENGTH];
  char tempbuffer[800];
  int foundtarget = 0;
  int foundgroup = 0;
  int hasholylight;

  one_argument(argument, target);

  hasholylight = IS_MOB(ch) ? 0 : ch->player->holyLite;

  send_to_char(
      "$B$7($4:$7)=======================================================================($4:$7)\n\r"
      "$7|$5/$7|                     $5Current Grouped Adventurers                       $7|$5/$7|\n\r"
      "$7($4:$7)=======================================================================($4:$7)$R\n\r",
      ch);

  if (*target)
  {
    sprintf(gWhoBuffer, "Searching for '$B%s$R'...\r\n", target);
    send_to_char(gWhoBuffer, ch);
  }

  clear_who_buffer();

  for (d = DC::getInstance()->descriptor_list; d; d = d->next)
  {
    foundtarget = 0;

    if ((d->connected) || (!CAN_SEE(ch, d->character)))
      continue;

    //  What the hell is this line supposed to be checking? -pir
    //  If this occurs, we got alot bigger problems than 'who_group'
    //      if (ch->desc->character != ch)
    //         continue;

    i = d->character;

    // If I'm the leader of my group, process it
    if ((!i->master) && (IS_AFFECTED(i, AFF_GROUP)))
    {
      foundgroup = 1; // we found someone!
      k = i;
      sprintf(tempbuffer, "\n\r"
                          "   $B$7[$4: $5%s $4:$7]$R\n\r"
                          "   Player kills: %-3d  Average level of victim: %d  Total kills: %-3d\n\r",
              k->group_name,
              IS_MOB(k) ? 0 : k->player->group_pkills,
              IS_MOB(k) ? 0 : (k->player->group_pkills ? (k->player->grpplvl / k->player->group_pkills) : 0),
              IS_MOB(k) ? 0 : k->player->group_kills);
      add_to_who(tempbuffer);

      // If we're searching, see if this is the target
      if (is_abbrev(target, GET_NAME(i)))
        foundtarget = 1;

      // First, if they're not anonymous
      if ((!IS_MOB(ch) && hasholylight) || (!IS_ANONYMOUS(k) || (k->clan == ch->clan && ch->clan)))
      {
        sprintf(tempbuffer,
                "   $B%-18s %-10s %-14s   Level %2d      $1($7Leader$1)$R \n\r",
                GET_NAME(k), races[(int)GET_RACE(k)].singular_name,
                pc_clss_types[(int)GET_CLASS(k)], GET_LEVEL(k));
      }
      else
      {
        sprintf(tempbuffer,
                "   $B%-18s %-10s Anonymous                      $1($7Leader$1)$R \n\r",
                GET_NAME(k), races[(int)GET_RACE(k)].singular_name);
      }
      add_to_who(tempbuffer);

      // loop through my followers and process them
      for (f = k->followers; f; f = f->next)
      {
        if (IS_PC(f->follower))
          if (IS_AFFECTED(f->follower, AFF_GROUP))
          {
            // If we're searching, see if this is the target
            if (is_abbrev(target, GET_NAME(f->follower)))
              foundtarget = 1;
            // First if they're not anonymous
            if (!IS_ANONYMOUS(f->follower) || (f->follower->clan == ch->clan && ch->clan))
              sprintf(tempbuffer, "   %-18s %-10s %-14s   Level %2d\n\r",
                      GET_NAME(f->follower), races[(int)GET_RACE(f->follower)].singular_name,
                      pc_clss_types[(int)GET_CLASS(f->follower)], GET_LEVEL(f->follower));
            else
              sprintf(tempbuffer,
                      "   %-18s %-10s Anonymous            \n\r",
                      GET_NAME(f->follower), races[(int)GET_RACE(f->follower)].singular_name);
            add_to_who(tempbuffer);
          }
      } // for f = k->followers
    }   //  ((!i->master) && (IS_AFFECTED(i, AFF_GROUP)) )

    // if we're searching (target exists) and we didn't find it, clear out
    // the buffer cause we only want the target's group.
    // If we found it, send it out, clear the buffer, and keep going in case someone else
    // matches the same target pattern.   ('whog a' gets Anarchy and Alpha's groups)
    // -pir
    if (*target && !foundtarget)
    {
      clear_who_buffer();
      foundgroup = 0;
    }
    else if (*target && foundtarget)
    {
      send_to_char(gWhoBuffer, ch);
      clear_who_buffer();
    }
  } // End for(d).

  if (0 == foundgroup)
    add_to_who("\n\rNo groups found.\r\n");

  // page it to the player.  the 1 tells page_string to make it's own copy of the data
  page_string(ch->desc, gWhoBuffer, 1);
  return eSUCCESS;
}

int do_whosolo(Character *ch, char *argument, int cmd)
{
  Connection *d;
  Character *i;
  char tempbuffer[800];
  char buf[MAX_INPUT_LENGTH + 1];
  bool foundtarget;

  one_argument(argument, buf);

  send_to_char(
      "$B$7($4:$7)=======================================================================($4:$7)\n\r"
      "$7|$5/$7|                      $5Current SOLO Adventurers                         $7|$5/$7|\n\r"
      "$7($4:$7)=======================================================================($4:$7)$R\n\r"
      "   $BName            Race      Class        Level  PKs Deaths Avg-vict-level$R\n\r",
      ch);

  clear_who_buffer();

  for (d = DC::getInstance()->descriptor_list; d; d = d->next)
  {
    foundtarget = false;

    if ((d->connected) || !(i = d->character) || (!CAN_SEE(ch, i)))
      continue;

    if (is_abbrev(buf, GET_NAME(i)))
      foundtarget = true;

    if (*buf && !foundtarget)
      continue;

    if (GET_LEVEL(i) <= MORTAL)
      if (!IS_AFFECTED(i, AFF_GROUP))
      {
        if (!IS_ANONYMOUS(i) || (i->clan && i->clan == ch->clan))
          sprintf(tempbuffer,
                  "   %-15s %-9s %-13s %2d     %-4d%-7d%d\n\r",
                  i->name,
                  races[(int)GET_RACE(i)].singular_name,
                  pc_clss_types[(int)GET_CLASS(i)], GET_LEVEL(i),
                  IS_MOB(i) ? 0 : i->player->totalpkills,
                  IS_MOB(i) ? 0 : i->player->pdeathslogin,
                  IS_MOB(i) ? 0 : (i->player->totalpkills ? (i->player->totalpkillslv / i->player->totalpkills) : 0));
        else
          sprintf(tempbuffer,
                  "   %-15s %-9s Anonymous            %-4d%-7d%d\n\r",
                  i->name,
                  races[(int)GET_RACE(i)].singular_name,
                  IS_MOB(i) ? 0 : i->player->totalpkills,
                  IS_MOB(i) ? 0 : i->player->pdeathslogin,
                  IS_MOB(i) ? 0 : (i->player->totalpkills ? (i->player->totalpkillslv / i->player->totalpkills) : 0));
        add_to_who(tempbuffer);
      } // if is affected by group
  }     // End For Loop.

  // page it to the player.  the 1 tells page_string to make it's own copy of the data
  page_string(ch->desc, gWhoBuffer, 1);
  return eSUCCESS;
}

command_return_t Character::do_who(QStringList arguments, int cmd)
{
  const QStringList immortFields = {
      "   Immortal  ",
      "  Architect  ",
      "    Deity    ",
      "   Overseer  ",
      "   Divinity  ",
      "   --------  ",
      " Coordinator ",
      "   --------  ",
      " Implementer "};

  quint64 lowlevel{}, highlevel{UINT64_MAX}, numPC{}, numImmort{};
  bool anoncheck{}, sexcheck{}, guidecheck{}, lfgcheck{}, charcheck{}, nomatch{}, charmatchistrue{}, addimmbuf{};
  Character::sex_t sextype{};
  QString charname, class_found, race_found;
  for (const auto &oneword : arguments)
  {
    bool ok = false;
    auto levelarg = oneword.toULongLong(&ok);
    if (ok)
    {
      if (!lowlevel)
      {
        lowlevel = levelarg;
      }
      else if (lowlevel > levelarg)
      {
        highlevel = lowlevel;
        lowlevel = levelarg;
      }
      else
      {
        highlevel = levelarg;
      }
      continue;
    }

    // note that for all these, we don't 'continue' cause we want
    // to check for a name match at the end in case some annoying mortal
    // named himself "Anonymous" or "Penis", etc.
    if (is_abbrev(oneword, "anonymous"))
    {
      anoncheck = true;
    }
    else if (is_abbrev(oneword, "penis") || is_abbrev(oneword, "male"))
    {
      sexcheck = true;
      sextype = Character::sex_t::MALE;
    }
    else if (is_abbrev(oneword, "guide"))
    {
      guidecheck = true;
    }
    else if (is_abbrev(oneword, "vagina") || is_abbrev(oneword, "female"))
    {
      sexcheck = true;
      sextype = Character::sex_t::FEMALE;
    }
    else if (is_abbrev(oneword, "other") || is_abbrev(oneword, "neutral"))
    {
      sexcheck = true;
      sextype = Character::sex_t::NEUTRAL;
    }
    else if (is_abbrev(oneword, "lfg"))
    {
      lfgcheck = true;
    }
    else
    {
      auto is_abbreviation = [&](auto fullname)
      { return is_abbrev(oneword, fullname); };

      auto it = std::find_if(begin(class_names), end(class_names), is_abbreviation);
      if (it != std::end(class_names))
      {
        class_found = *it;
      }
      else
      {
        it = std::find_if(begin(race_names), end(race_names), is_abbreviation);
        if (it != std::end(race_names))
        {
          race_found = *it;
        }
        else
        {
          charname = oneword;
          charcheck = true;
        }
      }
    }
  } // end of for loop

  // Display the actual stuff
  send("[$4:$R]===================================[$4:$R]\n\r"
       "|$5/$R|      $BDenizens of Dark Castle$R      |$5/$R|\n\r"
       "[$4:$R]===================================[$4:$R]\n\r\n\r");

  QString buf;
  QString immbuf;
  bool hasholylight = IS_MOB(this) ? false : player->holyLite;
  for (auto d = DC::getInstance()->descriptor_list; d; d = d->next)
  {
    QString infoBuf;
    QString extraBuf;
    QString tailBuf;
    QString preBuf;

    // we have an invalid match arg, so nothing is going to match
    if (nomatch)
    {
      break;
    }

    if ((d->connected) && (d->connected) != Connection::states::WRITE_BOARD && (d->connected) != Connection::states::EDITING && (d->connected) != Connection::states::EDIT_MPROG)
    {
      continue;
    }

    Character *i{};
    if (d->original)
    {
      i = d->original;
    }
    else
    {
      i = d->character;
    }

    if (IS_NPC(i))
    {
      continue;
    }
    if (!CAN_SEE(this, i))
    {
      continue;
    }
    // Level checks.  These happen no matter what
    if (GET_LEVEL(i) < lowlevel)
    {
      continue;
    }

    if (GET_LEVEL(i) > highlevel)
    {
      continue;
    }

    if (!class_found.isEmpty() && !hasholylight && (!i->clan || i->clan != this->clan) && IS_ANONYMOUS(i) && GET_LEVEL(i) < MIN_GOD)
    {
      continue;
    }
    if (lowlevel > 0 && IS_ANONYMOUS(i) && !hasholylight)
    {
      continue;
    }

    // Skip string based checks if our name matches
    if (!charcheck || !is_abbrev(charname, GET_NAME(i)))
    {
      if (!class_found.isEmpty() && i->getClassName() != class_found && !charmatchistrue)
      {
        continue;
      }
      if (anoncheck && !IS_ANONYMOUS(i) && !charmatchistrue)
      {
        continue;
      }
      if (sexcheck && (GET_SEX(i) != sextype || (IS_ANONYMOUS(i) && !hasholylight)) && !charmatchistrue)
      {
        continue;
      }
      if (lfgcheck && !DC::isSet(i->player->toggles, Player::PLR_LFG))
      {
        continue;
      }
      if (guidecheck && !DC::isSet(i->player->toggles, Player::PLR_GUIDE_TOG))
      {
        continue;
      }
      if (!race_found.isEmpty() && i->getRaceName() != race_found && !charmatchistrue)
      {
        continue;
      }
    }

    if (charcheck)
    {
      if (!is_abbrev(charname, GET_NAME(i)))
      {
        continue;
      }
    }

    addimmbuf = false;
    if (GET_LEVEL(i) > MORTAL)
    {
      /* Immortals can't be anonymous */
      if (!str_cmp(GET_NAME(i), "Urizen"))
      {
        infoBuf = "   Meatball  ";
      }
      else if (!str_cmp(GET_NAME(i), "Julian"))
      {
        infoBuf = "    $B$7S$4a$7l$4m$7o$4n$R   ";
      }
      else if (!strcmp(GET_NAME(i), "Apocalypse"))
      {
        infoBuf = "    $5Moose$R    ";
      }
      else if (!strcmp(GET_NAME(i), "Pirahna"))
      {
        infoBuf = "   $B$4>$5<$1($2($1($5:$4>$R   ";
      }
      else if (!strcmp(GET_NAME(i), "Petra"))
      {
        infoBuf = "    $B$1R$2o$1a$2d$1i$2e$R   ";
      }
      else
      {
        infoBuf = immortFields.value(GET_LEVEL(i) - IMMORTAL);
      }

      if (GET_LEVEL(this) >= IMMORTAL && !IS_MOB(i) && i->player->wizinvis > 0)
      {
        if (!IS_MOB(i) && i->player->incognito == true)
        {
          extraBuf = QString(" (Incognito / WizInvis %1)").arg(i->player->wizinvis);
        }
        else
        {
          extraBuf = QString(" (WizInvis %1)").arg(i->player->wizinvis);
        }
      }
      numImmort++;
      addimmbuf = true;
    }
    else
    {
      if (!IS_ANONYMOUS(i) || (clan && clan == i->clan) || hasholylight)
      {
        infoBuf = QString(" $B$5%1$7-$1%2  $2%3$R$7 ").arg(GET_LEVEL(i)).arg(pc_clss_abbrev[(int)GET_CLASS(i)]).arg(race_abbrev[(int)GET_RACE(i)]);
      }
      else
      {
        infoBuf = QString("  $6-==-   $B$2%1$R ").arg(race_abbrev[(int)GET_RACE(i)]);
      }
      numPC++;
    }

    if ((d->connected) == Connection::states::WRITE_BOARD || (d->connected) == Connection::states::EDITING ||
        (d->connected) == Connection::states::EDIT_MPROG)
    {
      tailBuf = "$1$B(writing) ";
    }

    if (DC::isSet(i->player->toggles, Player::PLR_GUIDE_TOG))
    {
      preBuf = "$7$B(Guide)$R ";
    }

    if (DC::isSet(i->player->toggles, Player::PLR_LFG))
    {
      tailBuf += "$3(LFG) ";
    }

    if (IS_AFFECTED(i, AFF_CHAMPION))
    {
      tailBuf += "$B$4(Champion)$R";
    }

    auto clanPtr = get_clan(i);
    if (i->clan && clanPtr && GET_LEVEL(i) < OVERSEER)
    {
      buf = QString("[%1] %2$3%3 %4 %5 $2[%6$R$2] %7$R\n\r").arg(infoBuf).arg(preBuf).arg(GET_SHORT(i)).arg(QString(i->title)).arg(extraBuf).arg(clanPtr->name).arg(tailBuf);
    }
    else
    {
      buf = QString("[%1] %2$3%3 %4 %5 %6$R\n\r").arg(infoBuf).arg(preBuf).arg(GET_SHORT(i)).arg(QString(i->title)).arg(extraBuf).arg(tailBuf);
    }

    if (addimmbuf)
    {
      immbuf += buf;
    }
    else
    {
      send(buf);
    }
  }

  if (numPC && numImmort)
  {
    send("\n\r");
  }

  if (numImmort)
  {
    send(immbuf);
  }

  send(QString("\n\r"
               "    Visible Players Connected:   %1\n\r"
               "    Visible Immortals Connected: %2\n\r"
               "    (Max this boot is %3)\n\r")
           .arg(numPC)
           .arg(numImmort)
           .arg(max_who));

  return eSUCCESS;
}

int do_whoarena(Character *ch, char *argument, int cmd)
{
  int count = 0;
  clan_data *clan;

  send_to_char("\n\rPlayers in the Arena:\n\r--------------------------\n\r", ch);

  if (GET_LEVEL(ch) <= MORTAL)
  {
    const auto &character_list = DC::getInstance()->character_list;
    for (const auto &tmp : character_list)
    {
      if (CAN_SEE(ch, tmp))
      {
        if (DC::isSet(DC::getInstance()->world[tmp->in_room].room_flags, ARENA) && !DC::isSet(DC::getInstance()->world[tmp->in_room].room_flags, NO_WHERE))
        {
          if ((tmp->clan) && (clan = get_clan(tmp)) && GET_LEVEL(tmp) < IMMORTAL)
            csendf(ch, "%-20s - [%s$R]\n\r", GET_NAME(tmp), clan->name);
          else
            csendf(ch, "%-20s\n\r", GET_NAME(tmp));
          count++;
        }
      }
    }

    if (count == 0)
      csendf(ch, "\n\rThere are no visible players in the arena.\r\n");

    return eSUCCESS;
  }

  // If they're here that means they're a god
  const auto &character_list = DC::getInstance()->character_list;
  for (const auto &tmp : character_list)
  {
    if (CAN_SEE(ch, tmp))
    {
      if (DC::isSet(DC::getInstance()->world[tmp->in_room].room_flags, ARENA))
      {
        if ((tmp->clan) && (clan = get_clan(tmp)) && GET_LEVEL(tmp) < IMMORTAL)
          csendf(ch, "%-20s  Level: %-3d  Hit: %-5d  Room: %-5d - [%s$R]\n\r",
                 GET_NAME(tmp),
                 GET_LEVEL(tmp), tmp->getHP(), tmp->in_room, clan->name);
        else
          csendf(ch, "%-20s  Level: %-3d  Hit: %-5d  Room: %-5d\n\r",
                 GET_NAME(tmp),
                 GET_LEVEL(tmp), tmp->getHP(), tmp->in_room);
        count++;
      }
    }
  }

  if (count == 0)
    csendf(ch, "\n\rThere are no visible players in the arena.\r\n");
  return eSUCCESS;
}

int do_where(Character *ch, char *argument, int cmd)
{
  class Connection *d;
  int zonenumber;
  char buf[MAX_INPUT_LENGTH];

  one_argument(argument, buf);

  if (GET_LEVEL(ch) >= IMMORTAL && *buf && !strcmp(buf, "all"))
  { //  immortal noly, shows all
    send_to_char("All Players:\n\r--------\n\r", ch);
    for (d = DC::getInstance()->descriptor_list; d; d = d->next)
    {
      if (d->character && (d->connected == Connection::states::PLAYING) && (CAN_SEE(ch, d->character)) && (d->character->in_room != DC::NOWHERE))
      {
        if (d->original)
        { // If switched
          csendf(ch, "%-20s - %s$R [%d] In body of %s\n\r", d->original->name, DC::getInstance()->world[d->character->in_room].name,
                 DC::getInstance()->world[d->character->in_room].number, fname(d->character->name));
        }
        else
        {
          csendf(ch, "%-20s - %s$R [%d]\n\r",
                 d->character->name, DC::getInstance()->world[d->character->in_room].name, DC::getInstance()->world[d->character->in_room].number);
        }
      }
    } // for
  }
  else if (GET_LEVEL(ch) >= IMMORTAL && *buf)
  { // immortal only, shows ONE person
    send_to_char("Search of Players:\n\r--------\n\r", ch);
    for (d = DC::getInstance()->descriptor_list; d; d = d->next)
    {
      if (d->character && (d->connected == Connection::states::PLAYING) && (CAN_SEE(ch, d->character)) && (d->character->in_room != DC::NOWHERE))
      {
        if (d->original)
        { // If switched
          if (is_abbrev(buf, d->original->name))
          {
            csendf(ch, "%-20s - %s$R [%d] In body of %s\n\r", d->original->name, DC::getInstance()->world[d->character->in_room].name,
                   DC::getInstance()->world[d->character->in_room].number, fname(d->character->name));
          }
        }
        else
        {
          if (is_abbrev(buf, d->character->name))
          {
            csendf(ch, "%-20s - %s$R [%d]\n\r",
                   d->character->name, DC::getInstance()->world[d->character->in_room].name, DC::getInstance()->world[d->character->in_room].number);
          }
        }
      }
    } // for
  }
  else
  { // normal, mortal where
    zonenumber = DC::getInstance()->world[ch->in_room].zone;
    send_to_char("Players in your vicinity:\n\r-------------------------\n\r", ch);
    if (DC::isSet(DC::getInstance()->world[ch->in_room].room_flags, NO_WHERE))
      return eFAILURE;
    for (d = DC::getInstance()->descriptor_list; d; d = d->next)
    {
      if (d->character && (d->connected == Connection::states::PLAYING) && (d->character->in_room != DC::NOWHERE) &&
          !DC::isSet(DC::getInstance()->world[d->character->in_room].room_flags, NO_WHERE) &&
          CAN_SEE(ch, d->character) && !IS_MOB(d->character) /*Don't show snooped mobs*/)
      {
        if (DC::getInstance()->world[d->character->in_room].zone == zonenumber)
          csendf(ch, "%-20s - %s$R\n\r", d->character->name,
                 DC::getInstance()->world[d->character->in_room].name);
      }
    }
  }

  return eSUCCESS;
}
