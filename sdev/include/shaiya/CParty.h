#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shaiya/include/common.h>
#include <shaiya/include/common/PartyTypes.h>
#include "PartyUser.h"
#include "SNode.h"

namespace shaiya
{
    struct CUser;

    #pragma pack(push, 1)
    struct CParty : SNode
    {
        static constexpr int MaxUsers = 150;

        uint32_t id;                 //0x08
        int32_t bossIndex;           //0x0C
        int32_t userCount;           //0x10
        // 0x14
        Array<PartyUser, MaxUsers> users;  //0x14
        PAD(4);
        DivType divType;                   //0x4C8
        int32_t divSeq;                    //0x4CC
        int32_t maxUserLevel;              //0x4D0
        int32_t subBossIndex;              //0x4D4
        bool isUnion;                      //0x4D8
        bool isAutoJoin;                   //0x4D9
        PAD(2);
        CRITICAL_SECTION cs;               //0x4DC
        PAD(32);
        // 0x514

        static CUser* FindUser(CParty* party/*edi*/, unsigned objectId);
        static bool IsPartyBoss(CParty* party/*esi*/, CUser* user);
        static bool IsPartySubBoss(CParty* party/*esi*/, CUser* user);
        static int GetGroup(CParty* party/*eax*/, CUser* user);
        static CUser* GetPartyBoss(CParty* party/*esi*/);
        static CUser* GetPartySubBoss(CParty* party/*esi*/);
        static void LeaveParty(CUser* user/*eax*/, CParty* party/*ecx*/);
        static void Send(CParty* party/*esi*/, void* packet/*ecx*/, int length/*eax*/);
    };
    #pragma pack(pop)

    static_assert(sizeof(CParty) == 0x514);
}
