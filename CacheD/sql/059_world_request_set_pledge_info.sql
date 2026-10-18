-- Current Cached.exe primary opcode 58 (RequestSetPledgeInfo) persistence.
--
-- CPledgeDb::SetPledgeInfo fixes this three-input ABI and the exact 22-field
-- whitelist. Compatible modern donors implement the same reachable effect
-- through validated dynamic SQL. Explicit branches avoid dynamic identifiers
-- while preserving every current selector and invalid-field failure.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_setpledgeinfo"(
    p_field_name text,
    p_field_data integer,
    p_pledge_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    CASE p_field_name
    WHEN 'alliance_id' THEN
        UPDATE "world"."pledge" SET "alliance_id" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'challenge_time' THEN
        UPDATE "world"."pledge" SET "challenge_time" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'root_name_value' THEN
        UPDATE "world"."pledge" SET "root_name_value" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'now_war_id' THEN
        UPDATE "world"."pledge" SET "now_war_id" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'oust_time' THEN
        UPDATE "world"."pledge" SET "oust_time" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'skill_level' THEN
        UPDATE "world"."pledge" SET "skill_level" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'castle_id' THEN
        UPDATE "world"."pledge" SET "castle_id" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'agit_id' THEN
        UPDATE "world"."pledge" SET "agit_id" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'fortress_id' THEN
        UPDATE "world"."pledge" SET "fortress_id" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'rank' THEN
        UPDATE "world"."pledge" SET "rank" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'name_value' THEN
        UPDATE "world"."pledge" SET "name_value" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'status' THEN
        UPDATE "world"."pledge" SET "status" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'private_flag' THEN
        UPDATE "world"."pledge" SET "private_flag" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'crest_id' THEN
        UPDATE "world"."pledge" SET "crest_id" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'is_guilty' THEN
        UPDATE "world"."pledge" SET "is_guilty" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'dismiss_reserved_time' THEN
        UPDATE "world"."pledge" SET "dismiss_reserved_time" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'alliance_ousted_time' THEN
        UPDATE "world"."pledge" SET "alliance_ousted_time" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'alliance_withdraw_time' THEN
        UPDATE "world"."pledge" SET "alliance_withdraw_time" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'alliance_dismiss_time' THEN
        UPDATE "world"."pledge" SET "alliance_dismiss_time" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'ruler_id' THEN
        UPDATE "world"."pledge" SET "ruler_id" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'new_emblem_id' THEN
        UPDATE "world"."pledge" SET "new_emblem_id" = p_field_data WHERE "pledge_id" = p_pledge_id;
    WHEN 'mastery_reset_time' THEN
        UPDATE "world"."pledge" SET "mastery_reset_time" = p_field_data WHERE "pledge_id" = p_pledge_id;
    ELSE
        RAISE EXCEPTION 'lin_SetPledgeInfo: invalid field [%]', p_field_name;
    END CASE;
END;
$function$;

COMMIT;
