-- Current Cached.exe extended requests 261 through 270.
--
-- Cached.exe fixes every callable ABI, result order and Execute/Fetch branch.
-- Classic 3.0, modified 287 and Ertheia keep identical substantive bodies
-- for all procedures below. lin_RegTotalCountCommission is not repeated here:
-- its matching one-input/one-output PostgreSQL body already exists in
-- migration 026.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getregisteredcommission"(
    p_char_id integer
) RETURNS TABLE (
    commission_id bigint,
    item_commission_first_type integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        commission."commission_id",
        commission."item_commission_first_type"
    FROM "world"."commission" AS commission
    WHERE commission."char_id" = p_char_id
      AND commission."buyer_id" = 0
      AND commission."item_delete_time" IS NULL;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getsearchcommission"(
    p_depth integer,
    p_item_commission_type integer,
    p_item_general integer,
    p_item_grade integer,
    p_item_name text
) RETURNS TABLE (
    commission_id bigint,
    item_commission_first_type integer
)
LANGUAGE sql
STABLE
AS $function$
    -- This condition is the direct boolean reduction of the donor's three
    -- depth branches and its general/grade/name sub-branches. In particular,
    -- grade 0 and grade 11 select both grade values.
    SELECT
        commission."commission_id",
        commission."item_commission_first_type"
    FROM "world"."commission" AS commission
    WHERE commission."buyer_id" = 0
      AND (
          p_depth = 0
          OR (
              p_depth = 1
              AND commission."item_commission_first_type" =
                  p_item_commission_type
          )
          OR (
              p_depth <> 0
              AND p_depth <> 1
              AND commission."item_commission_type" =
                  p_item_commission_type
          )
      )
      AND (
          p_item_general = -1
          OR commission."item_general" = p_item_general
      )
      AND (
          p_item_grade = -1
          OR (
              p_item_grade IN (0, 11)
              AND commission."item_grade" IN (0, 11)
          )
          OR (
              p_item_grade NOT IN (0, 11)
              AND commission."item_grade" = p_item_grade
          )
      )
      AND (
          "world"."mssql_text_eq"(p_item_name, '')
          OR commission."item_name" LIKE '%' || p_item_name || '%'
      );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deleteallcommission"()
RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."commission";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadinstantagitlottery"()
RETURNS TABLE (
    instant_agit_id integer,
    next_lottery_date integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        lottery."instant_agit_id",
        lottery."next_lottery_date"
    FROM "world"."instant_agit_lottery" AS lottery;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadinstantagit"()
RETURNS TABLE (
    instant_agit_id integer,
    pledge_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        agit."own_instant_agit_id",
        agit."pledge_id"
    FROM "world"."instant_agit" AS agit
    ORDER BY agit."own_instant_agit_id";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadinstantagitallapplicant"()
RETURNS TABLE (
    instant_agit_id integer,
    pledge_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        applicant."apply_instant_agit_id",
        applicant."pledge_id"::integer
    FROM "world"."instant_agit_apply" AS applicant
    ORDER BY applicant."apply_instant_agit_id";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadinstantagitapplicant"(
    p_instant_agit_id integer
) RETURNS TABLE (
    pledge_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT applicant."pledge_id"::integer
    FROM "world"."instant_agit_apply" AS applicant
    WHERE applicant."apply_instant_agit_id" = p_instant_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadinstantagitdeco"()
RETURNS TABLE (
    pledge_id integer,
    deco_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        deco."pledge_id",
        deco."deco_id"
    FROM "world"."instant_agit_deco" AS deco;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setinstantagitnextlottery"(
    p_instant_agit_id integer,
    p_next_lottery_date integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."instant_agit_lottery" AS lottery
    SET "next_lottery_date" = p_next_lottery_date
    WHERE lottery."instant_agit_id" = p_instant_agit_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."instant_agit_lottery" (
            "instant_agit_id",
            "next_lottery_date"
        ) VALUES (
            p_instant_agit_id,
            p_next_lottery_date
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setinstantagit"(
    p_pledge_id integer,
    p_instant_agit_id integer,
    p_rental_date double precision,
    p_return_date integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_apply_id integer;
    v_rental_date integer := p_rental_date::integer;
BEGIN
    INSERT INTO "world"."instant_agit" (
        "pledge_id",
        "own_instant_agit_id"
    ) VALUES (
        p_pledge_id,
        p_instant_agit_id
    );

    SELECT max(history."id")
    INTO v_apply_id
    FROM "world"."instant_agit_history" AS history
    WHERE history."pledge_id" = p_pledge_id
      AND history."instant_agit_id" = p_instant_agit_id;

    IF v_apply_id > 0 THEN
        UPDATE "world"."instant_agit_history" AS history
        SET "rental_date" = v_rental_date,
            "return_date" = p_return_date
        WHERE history."id" = v_apply_id;
    ELSE
        INSERT INTO "world"."instant_agit_history" (
            "pledge_id",
            "instant_agit_id",
            "rental_date",
            "return_date"
        ) VALUES (
            p_pledge_id,
            p_instant_agit_id,
            v_rental_date,
            p_return_date
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_resetinstantagitapplicant"(
    p_instant_agit_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."instant_agit_apply" AS applicant
    WHERE applicant."apply_instant_agit_id" = p_instant_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getagathionelapsedtime"(
    p_char_id integer,
    p_agathion_id integer
) RETURNS TABLE (
    elapsed_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT COALESCE(
        (
            SELECT agathion."elapsed_time"
            FROM "world"."agathion" AS agathion
            WHERE agathion."char_id" = p_char_id
              AND agathion."agathion_id" = p_agathion_id
        ),
        0
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveagathiondespawntime"(
    p_char_id integer,
    p_agathion_id integer,
    p_despawn_time integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agathion" AS agathion
    SET "despawn_time" = p_despawn_time,
        "summon_state" = 0,
        "elapsed_time" =
            agathion."elapsed_time" +
            p_despawn_time -
            agathion."last_update_time",
        "last_update_time" = p_despawn_time
    WHERE agathion."char_id" = p_char_id
      AND agathion."agathion_id" = p_agathion_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveagathionsummontime"(
    p_char_id integer,
    p_agathion_id integer,
    p_summon_time integer
) RETURNS TABLE (
    elapsed_time integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_summon_local timestamp without time zone;
    v_compare_local timestamp without time zone;
    v_compare_time integer;
    v_previous_update_time integer;
    v_state smallint;
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."agathion" AS agathion
        WHERE agathion."char_id" = p_char_id
          AND agathion."agathion_id" = p_agathion_id
    ) THEN
        v_summon_local :=
            to_timestamp(p_summon_time) AT TIME ZONE current_setting('TimeZone');

        IF extract(hour FROM v_summon_local) < 6
           OR (
               extract(hour FROM v_summon_local) = 6
               AND extract(minute FROM v_summon_local) < 30
           ) THEN
            v_compare_local :=
                date_trunc('day', "world"."mssql_getdate"()) - INTERVAL '1 day' +
                INTERVAL '6 hours 30 minutes';
        ELSE
            v_compare_local :=
                date_trunc('day', "world"."mssql_getdate"()) +
                INTERVAL '6 hours 30 minutes';
        END IF;

        SELECT agathion."last_update_time"
        INTO v_previous_update_time
        FROM "world"."agathion" AS agathion
        WHERE agathion."char_id" = p_char_id
          AND agathion."agathion_id" = p_agathion_id;

        v_compare_time := extract(
            epoch FROM (
                v_compare_local AT TIME ZONE current_setting('TimeZone')
            )
        )::integer;

        IF v_previous_update_time < v_compare_time THEN
            UPDATE "world"."agathion" AS agathion
            SET "elapsed_time" = 0,
                "despawn_time" = 0
            WHERE agathion."char_id" = p_char_id
              AND agathion."agathion_id" = p_agathion_id;
        END IF;

        SELECT agathion."summon_state"
        INTO v_state
        FROM "world"."agathion" AS agathion
        WHERE agathion."char_id" = p_char_id
          AND agathion."agathion_id" = p_agathion_id;

        IF v_state = 0 THEN
            UPDATE "world"."agathion" AS agathion
            SET "summon_time" = p_summon_time
            WHERE agathion."char_id" = p_char_id
              AND agathion."agathion_id" = p_agathion_id;
        END IF;

        UPDATE "world"."agathion" AS agathion
        SET "last_update_time" = p_summon_time,
            "summon_state" = 1
        WHERE agathion."char_id" = p_char_id
          AND agathion."agathion_id" = p_agathion_id;

        RETURN QUERY
        SELECT agathion."elapsed_time"
        FROM "world"."agathion" AS agathion
        WHERE agathion."char_id" = p_char_id
          AND agathion."agathion_id" = p_agathion_id;
    ELSE
        INSERT INTO "world"."agathion" (
            "char_id",
            "agathion_id",
            "summon_time",
            "despawn_time",
            "elapsed_time",
            "last_update_time",
            "summon_state"
        ) VALUES (
            p_char_id,
            p_agathion_id,
            p_summon_time,
            0,
            0,
            p_summon_time,
            1
        );
        RETURN QUERY SELECT 0;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateagathionelapsedtime"(
    p_char_id integer,
    p_agathion_id integer,
    p_update_time integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agathion" AS agathion
    SET "elapsed_time" =
            agathion."elapsed_time" +
            p_update_time -
            agathion."last_update_time",
        "last_update_time" = p_update_time
    WHERE agathion."char_id" = p_char_id
      AND agathion."agathion_id" = p_agathion_id;
$function$;

COMMIT;
