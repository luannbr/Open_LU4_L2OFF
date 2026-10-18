-- Current Cached.exe primary opcodes 192-193:
-- RequstSetTeamBattleStatus and RequestSetTeamBattleWinner.
--
-- Current machine code fixes the two/four signed-int input ABIs. Every
-- substantive full-arity donor has the same normalized relational body and
-- effect set for each procedure. Zero-parameter extender stubs are rejected.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_setteambattlestatus"(
    p_agit_id integer,
    p_new_team_battle_status integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit"
    SET "team_battle_status" = p_new_team_battle_status
    WHERE "agit"."id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setteambattlewinner"(
    p_agit_id integer,
    p_winner_pledge_id integer,
    p_master_pledge_id integer,
    p_is_final integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_is_final <> 0 THEN
        -- Battle royal: keep the winner and the master pledge.
        DELETE FROM "world"."team_battle_agit_pledge"
        WHERE "team_battle_agit_pledge"."agit_id" = p_agit_id
          AND "team_battle_agit_pledge"."pledge_id" <> p_winner_pledge_id
          AND "team_battle_agit_pledge"."pledge_id" <> p_master_pledge_id;

        DELETE FROM "world"."team_battle_agit_member"
        WHERE "team_battle_agit_member"."agit_id" = p_agit_id
          AND "team_battle_agit_member"."pledge_id" <> p_winner_pledge_id
          AND "team_battle_agit_member"."pledge_id" <> p_master_pledge_id;
    ELSE
        -- Final winner: keep only its pledge row and remove every member.
        DELETE FROM "world"."team_battle_agit_pledge"
        WHERE "team_battle_agit_pledge"."agit_id" = p_agit_id
          AND "team_battle_agit_pledge"."pledge_id" <> p_winner_pledge_id;

        DELETE FROM "world"."team_battle_agit_member"
        WHERE "team_battle_agit_member"."agit_id" = p_agit_id;
    END IF;
END;
$function$;

COMMIT;
