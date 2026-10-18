-- Current Cached.exe administrative AQ 103/127 pledge-member projection and
-- the shared CPledgeDb::LoadPledgeMember callable surface.
--
-- The current image fixes one integer input. The administrative handlers bind
-- only char_id, while CPledgeDb binds both char_id and pledge_type. Compatible
-- post-C1 database families unanimously keep the unrestricted active-user
-- projection below. C1's obsolete one-column TOP 40 form is not imported.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadpledgemember"(
    p_pledge_id integer
) RETURNS TABLE (
    char_id integer,
    pledge_type integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        character.char_id,
        character.pledge_type
    FROM "world"."user_data" AS character
    WHERE character.pledge_id = p_pledge_id
      AND character.account_id > 0;
$function$;

COMMIT;
