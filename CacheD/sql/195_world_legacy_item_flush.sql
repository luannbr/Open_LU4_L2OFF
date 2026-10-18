-- Current Cached.exe CAddData::AddItemData legacy menu path.
--
-- The current executable fixes the no-input lin_FlushItemName call and runs
-- it before parsing any itemcache row. Classic 3.0, modified 287 and Ertheia
-- contain the same schema-compatible body: DELETE every row from ItemData.
-- This destructive effect is kept even though the adjacent
-- lin_InsertItemName body no longer exists for the modern five-flag ItemData
-- layout. The incompatible old item-name insert remains absent.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_flushitemname"()
RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."itemdata";
$function$;

COMMIT;
