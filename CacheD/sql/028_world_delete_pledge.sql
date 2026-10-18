-- Current Cached.exe primary opcode 11 (RequestDeletePledge) database slice.
--
-- The current image fixes both one-int call ABIs.  lin_DeletePledge's body is
-- identical in the compatible classic-3.0, modified-287 and Ertheia donors.
-- it is kept in that exact statement order.  No available SQL donor
-- defines lin_PledgeContributionMemberDeleteAllInfo, so its delete below is
-- an ABI-constrained PostgreSQL reconstruction over the exact
-- stable contribution table consumed by the current CacheD code.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_deletepledge"(
    p_pledge_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_world_id integer;
BEGIN
    SELECT pledge.world_id
    INTO v_world_id
    FROM "world"."pledge" AS pledge
    WHERE pledge.pledge_id = p_pledge_id;

    DELETE FROM "world"."academy_member"
    WHERE pledge_id = p_pledge_id;

    DELETE FROM "world"."academy"
    WHERE pledge_id = p_pledge_id;

    DELETE FROM "world"."sub_pledge"
    WHERE pledge_id = p_pledge_id;

    DELETE FROM "world"."pledge_skill"
    WHERE pledge_id = p_pledge_id;

    UPDATE "world"."user_data"
    SET pledge_id = 0,
        pledge_type = 0,
        grade_id = 0
    WHERE pledge_id = p_pledge_id;

    DELETE FROM "world"."pledge"
    WHERE pledge_id = p_pledge_id;

    UPDATE "world"."castle"
    SET world_id = v_world_id,
        prev_castle_owner_id = 0
    WHERE world_id = v_world_id
      AND prev_castle_owner_id = p_pledge_id;
END;
$function$;

CREATE OR REPLACE FUNCTION
"world"."lin_pledgecontributionmemberdeleteallinfo"(
    p_pledge_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."pledge_contribution_member_history" AS contribution
    WHERE contribution.pledge_id = p_pledge_id;
$function$;

COMMIT;
