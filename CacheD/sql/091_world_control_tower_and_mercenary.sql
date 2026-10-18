-- Current Cached.exe primary opcodes 154 through 159.
--
-- The current machine fixes all seven procedure names, input widths and
-- result widths. Classic 3.0, modified 287 and Ertheia agree on the complete
-- control-tower and mercenary effects. Their UpdateMercenary text differs
-- from C1 only by the modern one-row failure check kept below.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadcontroltowerbyname"(
    p_name text
) RETURNS TABLE (
    residence_id integer,
    control_level integer,
    hp integer,
    status integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        tower."residence_id",
        tower."control_level",
        tower."hp",
        tower."status"
    FROM "world"."control_tower" AS tower
    WHERE "world"."mssql_text_eq"(tower."name", p_name);
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_insertintocontroltower"(
    p_name text,
    p_residence_id integer,
    p_control_level integer,
    p_hp integer,
    p_status integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."control_tower" (
        "name",
        "residence_id",
        "control_level",
        "hp",
        "status"
    ) VALUES (
        p_name,
        p_residence_id,
        p_control_level,
        p_hp,
        p_status
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatecontroltower"(
    p_control_level integer,
    p_hp integer,
    p_status integer,
    p_name text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_rows integer;
BEGIN
    UPDATE "world"."control_tower" AS tower
    SET
        "control_level" = p_control_level,
        "hp" = p_hp,
        "status" = p_status
    WHERE "world"."mssql_text_eq"(tower."name", p_name);

    GET DIAGNOSTICS v_rows = ROW_COUNT;
    IF v_rows <> 1 THEN
        RAISE EXCEPTION
            'Failed to Update Control Tower name = %.',
            p_name;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_insertintomercenary"(
    p_residence_id integer,
    p_npc_id integer,
    p_x integer,
    p_y integer,
    p_z integer,
    p_angle integer,
    p_hp integer,
    p_mp integer
) RETURNS TABLE (
    record_id integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_record_id integer;
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."mercenary" AS mercenary
        WHERE mercenary."x" = p_x
          AND mercenary."y" = p_y
          AND mercenary."z" = p_z
    ) THEN
        DELETE FROM "world"."mercenary" AS mercenary
        WHERE mercenary."x" = p_x
          AND mercenary."y" = p_y
          AND mercenary."z" = p_z;
    END IF;

    INSERT INTO "world"."mercenary" (
        "residence_id",
        "npc_id",
        "x",
        "y",
        "z",
        "angle",
        "hp",
        "mp"
    ) VALUES (
        p_residence_id,
        p_npc_id,
        p_x,
        p_y,
        p_z,
        p_angle,
        p_hp,
        p_mp
    )
    RETURNING "id" INTO v_record_id;

    RETURN QUERY SELECT v_record_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadmercenary"(
    p_residence_id integer
) RETURNS TABLE (
    id integer,
    npc_id integer,
    x integer,
    y integer,
    z integer,
    angle integer,
    hp integer,
    mp integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        mercenary."id",
        mercenary."npc_id",
        mercenary."x",
        mercenary."y",
        mercenary."z",
        mercenary."angle",
        mercenary."hp",
        mercenary."mp"
    FROM "world"."mercenary" AS mercenary
    WHERE mercenary."residence_id" = p_residence_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletemercenary"(
    p_residence_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."mercenary" AS mercenary
        WHERE mercenary."residence_id" = p_residence_id
    ) THEN
        DELETE FROM "world"."mercenary" AS mercenary
        WHERE mercenary."residence_id" = p_residence_id;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatemercenary"(
    p_id integer,
    p_x integer,
    p_y integer,
    p_z integer,
    p_angle integer,
    p_hp integer,
    p_mp integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_rows integer;
BEGIN
    UPDATE "world"."mercenary" AS mercenary
    SET
        "x" = p_x,
        "y" = p_y,
        "z" = p_z,
        "angle" = p_angle,
        "hp" = p_hp,
        "mp" = p_mp
    WHERE mercenary."id" = p_id;

    GET DIAGNOSTICS v_rows = ROW_COUNT;
    IF v_rows <> 1 THEN
        RAISE EXCEPTION
            'Failed to Update Mercenary id = %.',
            p_id;
    END IF;
END;
$function$;

COMMIT;
