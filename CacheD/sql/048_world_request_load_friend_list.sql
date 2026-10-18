-- Current Cached.exe primary opcode 44 (RequestLoadFriendList) database slice.
--
-- No substantive local donor keeps lin_LoadFriendInfo. The current CacheD
-- machine path nevertheless fixes one integer input and the exact result ABI:
-- UTF-16 name, smallint, smallint. The paired current L2Server decoder stores
-- those two shorts into FriendInfo's level and class fields respectively.
-- CFriends::UpdateFriendLevel/UpdateFriendClass confirm their
-- order. The existing current-derived user_data layout then fixes this
-- relational projection without claiming recovery of the unavailable T-SQL
-- source text.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadfriendinfo"(
    p_char_id integer
) RETURNS TABLE (
    char_name varchar(50),
    level smallint,
    character_class smallint
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        character.char_name,
        character.lev,
        character."class"
    FROM "world"."user_data" AS character
    WHERE character.char_id = p_char_id;
$function$;

COMMIT;
