-- Current Cached.exe primary opcode 10 (RequestUpdateUserPledge) database
-- slice.
--
-- Join/withdraw, academy, character-pledge, grade and sub-pledge-master bodies
-- are shared by the compatible classic-3.0/modified-287/Ertheia SQL donors.
-- the current CacheD call sites fix their parameter and result order.
-- pledge_contribution_member_history has the exact stable World52/modified-287
-- table shape. The older World103 report catalog does not contain this newer
-- relation.  The external ResetInfo procedure body is absent from the available
-- dumps. Its upsert below is constrained by all four current parameters and
-- by the exact columns later read by Cached.exe.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE TABLE IF NOT EXISTS "world"."pledge_contribution_member_history" (
    "pledge_id" integer NOT NULL,
    "user_id" integer NOT NULL,
    "user_name" varchar(50) NOT NULL,
    "pledge_type" integer NOT NULL,
    "period_rank" integer NOT NULL,
    "period_ratio" integer NOT NULL,
    "period_point" integer NOT NULL,
    "total_point" integer NOT NULL,
    CONSTRAINT "pk_pledge_contribution_member_history"
        PRIMARY KEY ("pledge_id", "user_id")
);

CREATE OR REPLACE FUNCTION "world"."lin_joinpledgemember"(
    p_pledge_id integer,
    p_pledge_type integer,
    p_member_id integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_rows integer := 0;
BEGIN
    UPDATE "world"."user_data" AS member
    SET pledge_id = p_pledge_id,
        pledge_type = p_pledge_type,
        grade_id = 0
    WHERE member.char_id = p_member_id;
    GET DIAGNOSTICS v_rows = ROW_COUNT;
    RETURN QUERY SELECT CASE WHEN v_rows = 1 THEN 1 ELSE 0 END;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_withdrawpledgemember"(
    p_pledge_id integer,
    p_member_id integer,
    p_pledge_type integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_rows integer := 0;
BEGIN
    UPDATE "world"."user_data" AS member
    SET pledge_id = 0,
        pledge_type = 0,
        grade_id = 0
    WHERE member.char_id = p_member_id
      AND member.pledge_id = p_pledge_id
      AND member.pledge_type = p_pledge_type;
    GET DIAGNOSTICS v_rows = ROW_COUNT;
    RETURN QUERY SELECT CASE WHEN v_rows = 1 THEN 1 ELSE 0 END;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_joinacademymember"(
    p_user_id integer,
    p_pledge_id integer,
    p_join_level integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."academy_member" (
        user_id,
        pledge_id,
        join_level,
        master_id,
        status,
        status_time
    ) VALUES (
        p_user_id,
        p_pledge_id,
        p_join_level,
        0,
        0,
        "world"."mssql_getdate"()
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savecharacterpledge"(
    p_pledge_id integer,
    p_char_id integer,
    p_pledge_type integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data" AS character
    SET pledge_id = p_pledge_id,
        pledge_type = p_pledge_type
    WHERE character.char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setpledgepowergrade"(
    p_grade_id integer,
    p_char_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data" AS character
    SET grade_id = p_grade_id
    WHERE character.char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setsubpledgemaster"(
    p_master_id integer,
    p_pledge_id integer,
    p_pledge_type integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."sub_pledge" AS sub_pledge
    SET master_id = p_master_id
    WHERE sub_pledge.pledge_id = p_pledge_id
      AND sub_pledge.type = p_pledge_type;
$function$;

CREATE OR REPLACE FUNCTION
"world"."lin_pledgecontributionmemberresetinfo"(
    p_pledge_id integer,
    p_user_id integer,
    p_period_point integer,
    p_total_point integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."pledge_contribution_member_history" (
        pledge_id,
        user_id,
        user_name,
        pledge_type,
        period_rank,
        period_ratio,
        period_point,
        total_point
    )
    SELECT
        p_pledge_id,
        p_user_id,
        character.char_name,
        character.pledge_type,
        0,
        0,
        p_period_point,
        p_total_point
    FROM "world"."user_data" AS character
    WHERE character.char_id = p_user_id
    ON CONFLICT (pledge_id, user_id) DO UPDATE
    SET user_name = EXCLUDED.user_name,
        pledge_type = EXCLUDED.pledge_type,
        period_rank = 0,
        period_ratio = 0,
        period_point = EXCLUDED.period_point,
        total_point = EXCLUDED.total_point;
$function$;

CREATE OR REPLACE FUNCTION
"world"."lin_pledgecontributionmemberdeleteinfo"(
    p_user_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."pledge_contribution_member_history" AS contribution
    WHERE contribution.user_id = p_user_id;
$function$;

COMMIT;
