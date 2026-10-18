#pragma once

// The current CItem rollback/commit branches keep the same three values as
// the C1 source-level enum.
enum TransactType : int {
    TransactType_Invalid = 0,
    TransactType_RollbackChanges = 1,
    TransactType_RollbackCreation = 2,
};
