-- Current Cached.exe primary opcode 56 (RequestDeleteSkill) persistence.
--
-- The current machine fixes unsigned character ID followed by signed subjob
-- and skill IDs. Six substantive full-arity-compatible donor families agree
-- on the single-row deletion effect below.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_delaquireskill"(
    p_char_id integer,
    p_subjob_id integer,
    p_skill_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_skill" AS skill
    WHERE skill."char_id" = p_char_id
      AND skill."skill_id" = p_skill_id
      AND COALESCE(skill."subjob_id", 0) = p_subjob_id;
$function$;

COMMIT;
