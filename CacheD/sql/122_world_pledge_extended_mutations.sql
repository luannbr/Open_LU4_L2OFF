BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

-- Extended request 63. Current Cached.exe fixes the four input bindings.
-- All full-arity compatible donor definitions agree on the validation and
-- insert behavior. A PostgreSQL exception gives the same
-- all-or-nothing statement effect as the donor's explicit transaction.
CREATE OR REPLACE FUNCTION "world"."lin_createsubpledge"(
    p_pledge_id integer,
    p_pledge_type integer,
    p_name text,
    p_master_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_name LIKE ' ' THEN
        RAISE EXCEPTION 'subpledge name has space: name = [%]', p_name;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_prohibit" AS prohibited
        WHERE "world"."mssql_text_eq"(prohibited."char_name", p_name)
    ) THEN
        RAISE EXCEPTION 'subpledge name is prohibited: name = [%]', p_name;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_prohibit_word" AS prohibited
        WHERE strpos(lower(p_name), lower(prohibited."words")) > 0
    ) THEN
        RAISE EXCEPTION 'subpledge name has prohibited word: name = [%]',
            p_name;
    END IF;

    INSERT INTO "world"."sub_pledge" (
        "pledge_id",
        "master_id",
        "type",
        "name"
    ) VALUES (
        p_pledge_id,
        p_master_id,
        p_pledge_type,
        p_name
    );
END;
$function$;

-- Extended request 66. All compatible donors use this exact two-key delete.
CREATE OR REPLACE FUNCTION "world"."lin_deletepledgeskill"(
    p_pledge_id integer,
    p_skill_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."pledge_skill" AS skill
    WHERE skill."pledge_id" = p_pledge_id
      AND skill."skill_id" = p_skill_id;
$function$;

-- Extended request 68. Cached.exe binds the third parameter through the
-- recovered SQL_DOUBLE overload even though compatible MSSQL procedures
-- declare it as int. Truncation before the integer relation write keeps
-- that driver/procedure conversion boundary.
CREATE OR REPLACE FUNCTION "world"."lin_setpowergrade"(
    p_pledge_id integer,
    p_grade_id integer,
    p_power double precision
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."pledge_power" (
        "pledge_id",
        "grade_id",
        "power_bits"
    ) VALUES (
        p_pledge_id,
        p_grade_id,
        trunc(p_power)::integer
    )
    ON CONFLICT ("pledge_id", "grade_id") DO UPDATE
    SET "power_bits" = EXCLUDED."power_bits";
$function$;

COMMIT;
