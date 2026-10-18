-- Current Cached.exe primary opcode 135 database-related startup counts.
--
-- The current handler fixes a no-input, seven-int32 result ABI and sends
-- result columns 1,2,3,4,7,6. The first five projections are identical in
-- the compatible Classic 3.0, modified 287, Ertheia and High Five donors.
-- Current paired L2Server code proves the two added fields:
-- columns six and seven are the required row counts for the unfiltered
-- lin_WOlympiadLoadAllUser and lin_WOlympiadLoadMatchCount startup streams.
-- Column five remains the donor's legacy Olympiad count. Cached.exe fetches
-- it but omits it from the packet.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getdbrelatedcounts"()
RETURNS TABLE (
    alliance_count integer,
    pledge_war_count integer,
    dismiss_reserved_count integer,
    nobless_count integer,
    legacy_match_count integer,
    world_olympiad_user_count integer,
    world_olympiad_match_count integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        (
            SELECT COUNT(alliance."id")::integer
            FROM "world"."alliance" AS alliance
        ),
        (
            SELECT COUNT(*)::integer
            FROM "world"."war_declare" AS war
            WHERE EXISTS (
                      SELECT 1
                      FROM "world"."pledge" AS challenger_pledge
                      WHERE challenger_pledge."pledge_id" = war."challenger"
                  )
              AND EXISTS (
                      SELECT 1
                      FROM "world"."pledge" AS challengee_pledge
                      WHERE challengee_pledge."pledge_id" = war."challengee"
                  )
        ),
        (
            SELECT COUNT(pledge."pledge_id")::integer
            FROM "world"."pledge" AS pledge
            WHERE pledge."status" = 3
        ),
        (
            SELECT COUNT(*)::integer
            FROM "world"."user_nobless" AS nobless
            WHERE EXISTS (
                SELECT 1
                FROM "world"."user_data" AS character
                WHERE character."char_id" = nobless."char_id"
            )
        ),
        (
            SELECT COUNT(*)::integer
            FROM "world"."olympiad_match_count"
        ),
        (
            SELECT COUNT(*)::integer
            FROM "world"."world_olympiad_user"
        ),
        (
            SELECT COUNT(*)::integer
            FROM "world"."world_olympiad_match_count"
        );
$function$;

COMMIT;
