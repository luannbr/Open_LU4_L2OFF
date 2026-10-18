-- Current Cached.exe primary opcode 43 (RequestLoadSkill) database slice.
-- The current image fixes both procedure names and every ordered ODBC input
-- and result type. All substantive signature-compatible modern donors keep
-- the same projections from the already-recovered stable relations.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadblocklist"(
    p_char_id integer
) RETURNS TABLE (
    block_char_id integer,
    block_memo text
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        block_entry."block_char_id",
        block_entry."block_memo"::text
    FROM "world"."user_blocklist" AS block_entry
    WHERE block_entry."char_id" = p_char_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getacquirepotentialpoint"(
    p_char_id integer,
    p_subjob_id integer
) RETURNS TABLE (
    type1 integer,
    type2 integer,
    type3 integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        point."type1",
        point."type2",
        point."type3"
    FROM "world"."user_potential_point" AS point
    WHERE point."user_charid" = p_char_id
      AND point."subjob" = p_subjob_id
$function$;

COMMIT;
