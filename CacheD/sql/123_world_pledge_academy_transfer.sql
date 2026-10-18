-- Extended requests 71-80 and the two existing CPledge support calls reached
-- by graduation. Current Cached.exe fixes every ABI below. Classic 3.0,
-- Ertheia and protocol-287 donors agree on the selected procedure bodies.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_initializebossrecord"(
    p_character_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_bossrecord" AS record
    WHERE record."char_id" = p_character_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_graduatepledgemember"(
    p_pledge_id integer,
    p_member_id integer,
    p_pledge_type integer
) RETURNS TABLE(result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    updated_count integer;
BEGIN
    UPDATE "world"."user_data" AS member
    SET "pledge_id" = 0,
        "pledge_type" = 0,
        "grade_id" = 0,
        "academy_pledge_id" = p_pledge_id
    WHERE member."char_id" = p_member_id
      AND member."pledge_id" = p_pledge_id
      AND member."pledge_type" = p_pledge_type;
    GET DIAGNOSTICS updated_count = ROW_COUNT;

    RETURN QUERY SELECT CASE WHEN updated_count = 1 THEN 1 ELSE 0 END;
END;
$function$;

-- CPledge::GraduateAcademyMember, reached by extended request 72.
CREATE OR REPLACE FUNCTION "world"."lin_graduateacademymember"(
    p_member_id integer,
    p_pledge_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."academy_member" AS member
    SET "status" = 1,
        "status_time" = "world"."mssql_getdate"()
    WHERE member."user_id" = p_member_id
      AND member."pledge_id" = p_pledge_id;
$function$;

-- CPledgeDb::LoadGraduateAcademyCount, refreshed after the same request.
CREATE OR REPLACE FUNCTION "world"."lin_loadgraduateacademycount"(
    p_pledge_id integer
) RETURNS TABLE(result integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT count(*)::integer
    FROM "world"."academy_member" AS member
    WHERE member."pledge_id" = p_pledge_id
      AND member."status" = 1;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setacademymaster"(
    p_master_id integer,
    p_member_id integer,
    p_pledge_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."academy_member" AS member
    SET "master_id" = p_master_id
    WHERE member."user_id" = p_member_id
      AND member."pledge_id" = p_pledge_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_requestpledgemastertransfer"(
    p_pledge_id integer,
    p_new_master_id integer
) RETURNS TABLE(result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    inserted_count integer;
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."pledge_master_transfer" AS transfer
        WHERE transfer."pledge_id" = p_pledge_id
          AND transfer."status" = 0
    ) THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    INSERT INTO "world"."pledge_master_transfer" (
        "pledge_id",
        "new_master_id",
        "status"
    ) VALUES (
        p_pledge_id,
        p_new_master_id,
        0
    );
    GET DIAGNOSTICS inserted_count = ROW_COUNT;
    RETURN QUERY SELECT inserted_count;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_requestpledgemastertransfercancel"(
    p_pledge_id integer
) RETURNS TABLE(result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    deleted_count integer;
BEGIN
    DELETE FROM "world"."pledge_master_transfer" AS transfer
    WHERE transfer."pledge_id" = p_pledge_id
      AND transfer."status" = 0;
    GET DIAGNOSTICS deleted_count = ROW_COUNT;
    RETURN QUERY SELECT deleted_count;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_renameacademy"(
    p_pledge_id integer,
    p_academy_name text
) RETURNS TABLE(result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    updated_count integer;
BEGIN
    UPDATE "world"."academy" AS academy
    SET "name" = p_academy_name
    WHERE academy."pledge_id" = p_pledge_id;
    GET DIAGNOSTICS updated_count = ROW_COUNT;
    RETURN QUERY SELECT updated_count;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_renamesubpledge"(
    p_pledge_id integer,
    p_pledge_type integer,
    p_subpledge_name text
) RETURNS TABLE(result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    updated_count integer;
BEGIN
    UPDATE "world"."sub_pledge" AS subpledge
    SET "name" = p_subpledge_name
    WHERE subpledge."pledge_id" = p_pledge_id
      AND subpledge."type" = p_pledge_type;
    GET DIAGNOSTICS updated_count = ROW_COUNT;
    RETURN QUERY SELECT updated_count;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_gettransferredpledgemaster"()
RETURNS TABLE(
    pledge_id integer,
    new_master_id integer,
    char_name text
)
LANGUAGE sql
STABLE
AS $function$
    SELECT DISTINCT
        latest."pledge_id",
        transfer."new_master_id",
        member."char_name"::text
    FROM (
        SELECT request."pledge_id",
               max(request."status_time") AS "status_time"
        FROM "world"."pledge_master_transfer" AS request
        WHERE request."status" = 1
        GROUP BY request."pledge_id"
    ) AS latest
    JOIN "world"."pledge_master_transfer" AS transfer
      ON transfer."pledge_id" = latest."pledge_id"
     AND transfer."status_time" = latest."status_time"
     AND transfer."status" = 1
    JOIN "world"."user_data" AS member
      ON member."char_id" = transfer."new_master_id"
     AND member."pledge_id" = latest."pledge_id";
$function$;

COMMIT;
