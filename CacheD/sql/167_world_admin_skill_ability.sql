-- Current Cached.exe AQ opcodes 176, 177 and 180.
--
-- The current image fixes all callable names, input/result order and the
-- twelve-row decision made by RequestAddActiveSkill. Classic 3.0, modified
-- 287 and Ertheia keep identical bodies for the older active-skill
-- procedures. Their add ABI predates only the current skill_sub_level input.
-- migration 019 and current CUser loading prove the matching
-- column. The current Delete spelling replaces the donors' abbreviated Del.
-- lin_DelSkillCoolTime and lin_AdminResetAbilityPoint have fully compatible,
-- identical bodies in every supplied donor family.

BEGIN;

CREATE OR REPLACE FUNCTION "world"."lin_admin_addnewactiveskill"(
    p_char_id integer,
    p_skill_id integer,
    p_skill_level integer,
    p_skill_sub_level integer,
    p_skill_duration integer,
    p_skill_selfcast integer
) RETURNS TABLE (
    row_count integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_row_count integer;
BEGIN
    SELECT count(*)::integer
    INTO v_row_count
    FROM "world"."user_newactiveskill" AS active_skill
    WHERE active_skill."char_id" = p_char_id;

    IF v_row_count < 12 THEN
        IF EXISTS (
            SELECT 1
            FROM "world"."user_newactiveskill" AS active_skill
            WHERE active_skill."char_id" = p_char_id
              AND active_skill."skill_id" = p_skill_id
        ) THEN
            -- The unanimous donor body refreshes only the duration for an
            -- already-present skill. Retain that observable behavior.
            UPDATE "world"."user_newactiveskill" AS active_skill
            SET "skill_duration" = p_skill_duration
            WHERE active_skill."char_id" = p_char_id
              AND active_skill."skill_id" = p_skill_id;
        ELSE
            INSERT INTO "world"."user_newactiveskill" (
                "char_id",
                "skill_id",
                "skill_level",
                "skill_duration",
                "skill_selfcast",
                "skill_sub_level"
            ) VALUES (
                p_char_id,
                p_skill_id,
                p_skill_level,
                p_skill_duration,
                p_skill_selfcast,
                p_skill_sub_level
            );
        END IF;
    END IF;

    RETURN QUERY SELECT v_row_count;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_admin_deletenewactiveskill"(
    p_char_id integer,
    p_skill_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_newactiveskill" AS active_skill
    WHERE active_skill."char_id" = p_char_id
      AND active_skill."skill_id" = p_skill_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_delskillcooltime"(
    p_char_id integer,
    p_subjob_id integer,
    p_skill_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_skill_reuse_delay" AS reuse_delay
    WHERE reuse_delay."char_id" = p_char_id
      AND reuse_delay."skill_id" = p_skill_id
      AND COALESCE(reuse_delay."subjob_id", 0) = p_subjob_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_adminresetabilitypoint"(
    p_char_id integer,
    p_subjob_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    DELETE FROM "world"."user_potential_skill" AS skill
    WHERE skill."user_charid" = p_char_id
      AND skill."subjob" = p_subjob_id;

    DELETE FROM "world"."user_potential_point" AS point
    WHERE point."user_charid" = p_char_id
      AND point."subjob" = p_subjob_id;
END;
$function$;

COMMIT;
