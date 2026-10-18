-- Current Cached.exe primary opcode 49 (GetPunishPacket) persistence.
--
-- The current image fixes one 32-bit character ID and four result columns in
-- this exact order. Every substantive full-arity-compatible donor selects the
-- same columns from user_punish without an ORDER BY. That unspecified row
-- order is kept deliberately.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getpunish"(
    p_char_id integer
) RETURNS TABLE (
    punish_id integer,
    punish_on integer,
    remain_game integer,
    remain_real integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        punishment."punish_id",
        punishment."punish_on"::integer,
        punishment."remain_game",
        punishment."remain_real"
    FROM "world"."user_punish" AS punishment
    WHERE punishment."char_id" = p_char_id
$function$;

COMMIT;
