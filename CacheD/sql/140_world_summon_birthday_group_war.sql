-- Extended requests 241-250. Current Cached.exe fixes the callable ABI and
-- branch behavior. The relational bodies below survive unchanged in the
-- compatible Classic 3.0 and 287 database families.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_deleteusersummon"(
    p_char_id integer,
    p_summon_db_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_summon_data" AS summons
    WHERE summons.char_id = p_char_id
      AND summons.summon_db_id = p_summon_db_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_receivementorreward"(
    p_mentor_id integer,
    p_mentee_id integer,
    p_item_count integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_mentor" AS mentors
    SET mentor_item_sum = mentors.mentor_item_sum + p_item_count
    WHERE mentors.mentor_id = p_mentor_id
      AND mentors.mentee_id = p_mentee_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setdormantgifttime"(
    p_account_id integer,
    p_char_id integer,
    p_type integer,
    p_gifttime integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_dormant" AS dormant
    SET gift_received_date = "world"."mssql_getdate"(),
        gifttime = p_gifttime
    WHERE dormant.account_id = p_account_id
      AND dormant.char_id = p_char_id
      AND dormant.type = p_type;

    IF NOT FOUND THEN
        INSERT INTO "world"."user_dormant" (
            account_id,
            char_id,
            gift_received_date,
            type,
            gifttime
        ) VALUES (
            p_account_id,
            p_char_id,
            "world"."mssql_getdate"(),
            p_type,
            p_gifttime
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_initgrouptogglestate"(
    p_char_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_group_toggle_skills" AS toggles
    WHERE toggles.char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadgrouptogglestate"(
    p_char_id integer
) RETURNS TABLE (
    toggle_group_id integer,
    toggle_skill_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT toggles.toggle_group_id,
           toggles.toggle_skill_id
    FROM "world"."user_group_toggle_skills" AS toggles
    WHERE toggles.char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savegrouptogglestate"(
    p_char_id integer,
    p_group_id integer,
    p_skill_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_group_toggle_skills" AS toggles
    SET toggle_skill_id = p_skill_id
    WHERE toggles.char_id = p_char_id
      AND toggles.toggle_group_id = p_group_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."user_group_toggle_skills" (
            char_id,
            toggle_group_id,
            toggle_skill_id
        ) VALUES (
            p_char_id,
            p_group_id,
            p_skill_id
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savepledgewarpoint"(
    p_challenger integer,
    p_challengee integer,
    p_point integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."war_declare" AS wars
    SET war_point = p_point
    WHERE wars.challenger = p_challenger
      AND wars.challengee = p_challengee;
$function$;

COMMIT;
