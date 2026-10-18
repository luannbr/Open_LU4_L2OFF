-- Current Cached.exe primary opcode 38 (RequestAquireSkill) database
-- contract. Current machine code fixes all five integer inputs. Compatible
-- modern SQL donors keep the same update-or-insert relation but predate the
-- current skill-sublevel input, so the fifth-column extension below is an
-- ABI-constrained reconstruction instead of a description of unavailable T-SQL.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_setaquireskill"(
    p_char_id integer,
    p_subjob_id integer,
    p_skill_id integer,
    p_skill_level integer,
    p_skill_sub_level integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."user_skill" AS skill
        WHERE skill."char_id" = p_char_id
          AND skill."skill_id" = p_skill_id
          AND COALESCE(skill."subjob_id", 0) = p_subjob_id
    ) THEN
        UPDATE "world"."user_skill" AS skill
        SET "skill_lev" = p_skill_level::smallint,
            "skill_sub_level" = p_skill_sub_level::smallint
        WHERE skill."char_id" = p_char_id
          AND skill."skill_id" = p_skill_id
          AND COALESCE(skill."subjob_id", 0) = p_subjob_id;
    ELSE
        INSERT INTO "world"."user_skill" (
            "char_id",
            "skill_id",
            "skill_lev",
            "to_end_time",
            "subjob_id",
            "is_lock",
            "skill_delay",
            "skill_sub_level"
        ) VALUES (
            p_char_id,
            p_skill_id,
            p_skill_level::smallint,
            0,
            p_subjob_id,
            0,
            0,
            p_skill_sub_level::smallint
        );
    END IF;
END;
$function$;

COMMIT;
