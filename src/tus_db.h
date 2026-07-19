// SPDX-FileCopyrightText: Copyright 2019-2026 rpcsn Project
// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <optional>
#include <QByteArray>
#include <QPair>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

// One Title User Storage variable slot. `set` is false when the slot has never
// been written (no DB row).
struct TusVariableRow {
    int64_t ownerUserId = 0;
    int32_t slotId = 0;
    bool set = false;
    int64_t variable = 0;
    int64_t oldVariable = 0; // value before the operation (Set / AddAndGet)
    uint64_t lastChanged = 0;
    int64_t lastChangedAuthorId = 0;
};

// One Title User Storage data slot. `data` is only populated for payload reads.
struct TusDataRow {
    int64_t ownerUserId = 0;
    int32_t slotId = 0;
    bool set = false;
    uint64_t dataSize = 0;
    QByteArray data;
    QByteArray info;
    uint64_t lastChanged = 0;
    int64_t lastChangedAuthorId = 0;
};

// Thin SQLite wrapper for TUS, constructed per-operation from a session's
// connection, mirroring ScoreDb.
class TusDb {
public:
    explicit TusDb(const QSqlDatabase& db) : m_db(db) {}

    // Writes (always target the caller; the dispatcher enforces owner == self).
    bool SetData(const QString& comId, int64_t owner, int32_t slot, const QByteArray& data,
                 const QByteArray& info, int64_t authorId, uint64_t now);
    // Virtual-user data write (sceNpTusSetData*VUser). Keyed by virtual_user.
    bool SetVUserData(const QString& comId, const QString& virtualUser, int32_t slot,
                      const QByteArray& data, const QByteArray& info, int64_t authorId,
                      uint64_t now);
    bool SetVariable(const QString& comId, int64_t owner, int32_t slot, int64_t value,
                     int64_t authorId, uint64_t now);
    // Virtual-user variable write (sceNpTusSetMultiSlotVariableVUser). Keyed by virtual_user.
    bool SetVUserVariable(const QString& comId, const QString& virtualUser, int32_t slot,
                          int64_t value, int64_t authorId, uint64_t now);

    // Atomic add-and-return. Wraps read+write in one transaction; Qt SQLite
    // serialises access on the single connection. Returns the row with both the
    // new value and the prior value (oldVariable).
    std::optional<TusVariableRow> AddAndGetVariable(const QString& comId, int64_t owner,
                                                    int32_t slot, int64_t delta, int64_t authorId,
                                                    uint64_t now);

    // Optional compare-and-set guard for AddAndGetVariableEx.
    struct ConflictCheck {
        bool hasAuthor = false;
        int64_t authorId = 0; // proceed only if current author == authorId
        bool hasDate = false;
        uint64_t date = 0; // proceed only if current lastChanged <= date
    };
    enum class AddStatus { Ok, Conflict, DbError };

    // AddAndGetVariable with virtual-user support and optional conflict checks.
    // virtualUser empty -> real account (keyed by `owner`); non-empty -> virtual user.
    std::optional<TusVariableRow> AddAndGetVariableEx(const QString& comId, int64_t owner,
                                                      const QString& virtualUser, int32_t slot,
                                                      int64_t delta, int64_t authorId, uint64_t now,
                                                      const ConflictCheck& check, AddStatus& status);

    // Conditional write (sceNpTusTryAndSetVariable). Compares `comparand` against the
    // current value using `opeType` (1..6 per SDK order); writes `value` if the
    // condition holds (or the slot is unset). Condition-not-met is NOT an error: the
    // row is returned unchanged. Author/date checks behave as in AddAndGetVariableEx.
    std::optional<TusVariableRow> TryAndSetVariableEx(const QString& comId, int64_t owner,
                                                      const QString& virtualUser, int32_t slot,
                                                      int32_t opeType, int64_t value,
                                                      int64_t comparand, int64_t authorId,
                                                      uint64_t now, const ConflictCheck& check,
                                                      AddStatus& status);

    // Reads (may target other users).
    std::optional<TusDataRow> GetData(const QString& comId, int64_t owner, int32_t slot,
                                      bool withPayload);
    // Virtual-user data read; keyed by virtual_user.
    std::optional<TusDataRow> GetVUserData(const QString& comId, const QString& virtualUser,
                                           int32_t slot, bool withPayload);
    // One row per requested slot, in order; unset slots come back with set=false.
    QVector<TusVariableRow> GetVariables(const QString& comId, int64_t owner,
                                         const QVector<int32_t>& slotIds);
    // Virtual-user variant: one row per requested slot for a virtual user.
    QVector<TusVariableRow> GetVUserVariables(const QString& comId, const QString& virtualUser,
                                              const QVector<int32_t>& slotIds);
    // Status-only (no payload); one row per (owner, slot) pair, in order.
    QVector<TusDataRow> GetDataStatuses(const QString& comId,
                                        const QVector<QPair<int64_t, int32_t>>& ownerSlotPairs);
    // Virtual-user status-only variant; one row per requested slot for a virtual user.
    QVector<TusDataRow> GetVUserDataStatuses(const QString& comId, const QString& virtualUser,
                                             const QVector<int32_t>& slotIds);

    bool DeleteSlots(const QString& comId, int64_t owner, const QVector<int32_t>& slotIds);
    // Virtual-user slot delete; clears both tus_vuser_data and tus_vuser_variable.
    bool DeleteVUserSlots(const QString& comId, const QString& virtualUser,
                          const QVector<int32_t>& slotIds);
    // Variable-only slot deletes (sceNpTusDeleteMultiSlotVariable*): clear just the variable row.
    bool DeleteVariableSlots(const QString& comId, int64_t owner, const QVector<int32_t>& slotIds);
    bool DeleteVUserVariableSlots(const QString& comId, const QString& virtualUser,
                                  const QVector<int32_t>& slotIds);

    // npid (== account.username) -> account user_id. Duplicates ScoreDb's lookup;
    // promote to Database if you want a single implementation.
    std::optional<int64_t> UserIdForNpid(const QString& npid);
    // Reverse of UserIdForNpid: account user_id -> npid (account.username).
    std::optional<QString> NpidForUserId(int64_t userId);

private:
    QSqlDatabase m_db;
};
