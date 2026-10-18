-- Current Cached.exe CAddData quest/skill import persistence.
--
-- The current machine fixes the four callable ABIs and the parser/call order.
-- Classic 3.0, modified 287 and Ertheia keep identical
-- DELETE/INSERT effects over the admitted QuestData and SkillData layouts.
-- The incompatible legacy ItemData pair is not defined here.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_flushquestname"()
RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."questdata";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_insertquestname"(
    p_id integer,
    p_name text,
    p_data text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."questdata" (
        "id",
        "name",
        "data"
    ) VALUES (
        p_id,
        p_name,
        p_data
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_flushskillname"()
RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."skilldata";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_insertskillname"(
    p_id integer,
    p_level integer,
    p_name text,
    p_skill_description text,
    p_magic_skill integer,
    p_activate_type text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."skilldata" (
        "id",
        "lev",
        "name",
        "skill_desc",
        "bismagic",
        "activate_type"
    ) VALUES (
        p_id,
        p_level::smallint,
        p_name,
        p_skill_description,
        p_magic_skill::smallint,
        p_activate_type
    );
$function$;

COMMIT;
