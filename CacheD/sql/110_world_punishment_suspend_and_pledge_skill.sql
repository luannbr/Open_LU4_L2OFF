-- Current Cached.exe primary opcodes 230, 231 and 233 persistence plus
-- admin AQ 126 added-service mutation.
--
-- The executable fixes all three callable ABIs and result widths. Classic
-- 3.0, modified 287 and Ertheia keep identical compatible bodies for the
-- punishment-history insert, suspended-pledge projection and user-skill
-- deletion. Opcode 232 reuses migration 019 lin_GetAquireSkill.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_addpunishmenthistory"(
    p_char_name text,
    p_char_id integer,
    p_log_type integer,
    p_account_name text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_create_date timestamp without time zone;
BEGIN
    SELECT character."create_date"
    INTO v_create_date
    FROM "world"."user_data" AS character
    WHERE character."char_id" = p_char_id;

    IF p_char_id > 0 THEN
        INSERT INTO "world"."user_history" (
            "char_name",
            "char_id",
            "log_action",
            "account_name",
            "create_date"
        ) VALUES (
            rtrim(p_char_name),
            p_char_id,
            p_log_type,
            p_account_name,
            COALESCE(v_create_date, "world"."mssql_getdate"())
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setuserservice"(
    p_char_id integer,
    p_service_flag integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_service_flag = 0 THEN
        DELETE FROM "world"."user_service" AS service
        WHERE service."char_id" = p_char_id;
    ELSIF EXISTS (
        SELECT 1
        FROM "world"."user_service" AS service
        WHERE service."char_id" = p_char_id
    ) THEN
        UPDATE "world"."user_service" AS service
        SET "service_flag" = p_service_flag,
            "reg_date" = "world"."mssql_getdate"()
        WHERE service."char_id" = p_char_id;
    ELSE
        INSERT INTO "world"."user_service" (
            "char_id",
            "service_flag"
        ) VALUES (
            p_char_id,
            p_service_flag
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getsuspendpledges"(
    p_now integer
) RETURNS TABLE (
    winner_pledge integer,
    loser_pledge integer,
    war_end_time integer,
    war_end_type integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT result."winner_pledge",
           result."loser_pledge",
           result."war_end_time",
           result."war_end_type"
    FROM "world"."pledge_war_result" AS result
    WHERE result."war_end_time" + (21 * 24 * 60 * 60) >= p_now;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_delpledgeskill"(
    p_char_id integer,
    p_skill_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_skill" AS skill
    WHERE skill."char_id" = p_char_id
      AND skill."skill_id" = p_skill_id;
$function$;

COMMIT;
