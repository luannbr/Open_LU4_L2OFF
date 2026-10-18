-- Current Cached.exe primary opcode 14 (RequestShortCutInfoPacket) database
-- slice. The current handler fixes two integer inputs and four ordered output
-- columns for lin_GetShortCut.
--
-- Classic 3.0, modified protocol-287 and Ertheia keep the identical query
-- below. This is a donor-consensus relational body constrained by the current
-- CacheD ABI, not a behavioral reconstruction.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getshortcut"(
    p_char_id integer,
    p_subjob_id integer
) RETURNS TABLE (
    slotnum integer,
    shortcut_type integer,
    shortcut_id integer,
    shortcut_macro text
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        shortcut.slotnum,
        shortcut.shortcut_type,
        shortcut.shortcut_id,
        shortcut.shortcut_macro::text
    FROM "world"."shortcut_data" AS shortcut
    WHERE shortcut.char_id = p_char_id
      AND shortcut.subjob_id = p_subjob_id
    ORDER BY shortcut.slotnum;
$function$;

COMMIT;
