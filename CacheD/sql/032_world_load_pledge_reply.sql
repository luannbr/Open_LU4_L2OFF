-- Current Cached.exe primary opcode 20 (RequestLoadPledge) database slice.
--
-- CUserDb::SimpleLoad fixes this seven-column positional result before it
-- performs the already recovered lin_LoadUserPledgeBonus(character, 0)
-- lookup.  Four full-arity-compatible donor families keep this identical
-- relational projection, while the current bind sequence fixes
-- all seven positions. Nickname remains nullable because the
-- target binds it with DBConn::BindNullable.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getsimpleuserdata"(
    p_char_id integer
) RETURNS TABLE (
    char_name varchar(50),
    nickname varchar(50),
    lev integer,
    character_class integer,
    gender integer,
    race integer,
    grade_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        character.char_name,
        character.nickname,
        character.lev::integer,
        character."class"::integer,
        character.gender::integer,
        character.race::integer,
        character.grade_id
    FROM "world"."user_data" AS character
    WHERE character.char_id = p_char_id;
$function$;

COMMIT;
