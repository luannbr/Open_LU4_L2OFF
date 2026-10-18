-- Current Cached.exe primary opcode 33 (RequestSetPledgeCrest) database
-- contracts. The pledge_crest relation is already owned by the exact stable
-- baseline in migration 001. This slice adds only the three procedure ABIs.
-- Every substantive signature-compatible donor family has the same effects.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_setpledgecrest"(
    p_bitmap_size integer,
    p_bitmap bytea
) RETURNS TABLE (crest_id integer)
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."pledge_crest" AS crest (
        bitmap_size,
        bitmap
    ) VALUES (
        p_bitmap_size::smallint,
        p_bitmap
    )
    RETURNING crest.crest_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpledgecrest"(
    p_crest_id integer
) RETURNS TABLE (
    bitmap_size integer,
    bitmap bytea
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        crest.bitmap_size::integer,
        crest.bitmap
    FROM "world"."pledge_crest" AS crest
    WHERE crest.crest_id = p_crest_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_delpledgecrest"(
    p_crest_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."pledge_crest" AS crest
    WHERE crest.crest_id = p_crest_id;
$function$;

COMMIT;
