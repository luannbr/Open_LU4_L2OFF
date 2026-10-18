-- Current Cached.exe disconnect/shutdown persistence boundary.
--
-- Callable names and parameter counts come from the current image:
--   * castle/pledge/user load and unload timestamps: one input each.
--   * lin_CharLogout: two inputs.
--   * lin_SaveCharacterSlot: 61 inputs (character id plus 60 slots)
--   * checksum set/check/get/crash recovery: 3/3/2/1 inputs.
-- The simple persistence bodies and checksum arithmetic are shared by the
-- compatible modern SQL donors.  The 61-input slot body is reconstructed
-- from the current bind order plus the modern 60-column load projection.
-- surviving donor lin_SaveCharacterSlot bodies stop at 34 inputs.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";
CREATE EXTENSION IF NOT EXISTS pgcrypto;

-- The physical column order follows the modified-287 table DDL and is now
-- present in the exact 61-column Rep_L2World52 report MDF.
-- It is not the current procedure ABI order: agathion/artifact slots are
-- returned before deco/waist/brooch/jewel slots by current
-- lin_GetUserDataByCharId. Both inspected report MDF variants carry the
-- one-row-per-character primary key, including the exact 61-column variant.
CREATE TABLE "world"."user_slot" (
    "char_id" integer NOT NULL,
    "st_underwear" integer,
    "st_right_ear" integer,
    "st_left_ear" integer,
    "st_neck" integer,
    "st_right_finger" integer,
    "st_left_finger" integer,
    "st_head" integer,
    "st_right_hand" integer,
    "st_left_hand" integer,
    "st_gloves" integer,
    "st_chest" integer,
    "st_legs" integer,
    "st_feet" integer,
    "st_back" integer,
    "st_both_hand" integer,
    "st_hair" integer,
    "st_hair2" integer,
    "st_right_bracelet" integer,
    "st_left_bracelet" integer,
    "st_deco1" integer,
    "st_deco2" integer,
    "st_deco3" integer,
    "st_deco4" integer,
    "st_deco5" integer,
    "st_deco6" integer,
    "st_waist" integer,
    "st_brooch" integer,
    "st_jewel1" integer,
    "st_jewel2" integer,
    "st_jewel3" integer,
    "st_jewel4" integer,
    "st_jewel5" integer,
    "st_jewel6" integer,
    "st_agathion_main" integer,
    "st_agathion_sub1" integer,
    "st_agathion_sub2" integer,
    "st_agathion_sub3" integer,
    "st_agathion_sub4" integer,
    "st_artifactbook" integer,
    "st_artifact_a1" integer,
    "st_artifact_a2" integer,
    "st_artifact_a3" integer,
    "st_artifact_a4" integer,
    "st_artifact_a5" integer,
    "st_artifact_a6" integer,
    "st_artifact_a7" integer,
    "st_artifact_a8" integer,
    "st_artifact_a9" integer,
    "st_artifact_a10" integer,
    "st_artifact_a11" integer,
    "st_artifact_a12" integer,
    "st_artifact_b1" integer,
    "st_artifact_b2" integer,
    "st_artifact_b3" integer,
    "st_artifact_c1" integer,
    "st_artifact_c2" integer,
    "st_artifact_c3" integer,
    "st_artifact_d1" integer,
    "st_artifact_d2" integer,
    "st_artifact_d3" integer,
    CONSTRAINT "pk_user_slot" PRIMARY KEY ("char_id")
);

CREATE OR REPLACE FUNCTION "world"."lin_setcastleloadtime"(
    p_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."castle"
    SET "load_date" = "world"."mssql_getdate"()
    WHERE "id" = p_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setcastleunloadtime"(
    p_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."castle"
    SET "unload_date" = "world"."mssql_getdate"()
    WHERE "id" = p_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setpledgeloadtime"(
    p_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."pledge"
    SET "load_date" = "world"."mssql_getdate"()
    WHERE "pledge_id" = p_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setpledgeunloadtime"(
    p_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."pledge"
    SET "unload_date" = "world"."mssql_getdate"()
    WHERE "pledge_id" = p_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setuserloadtime"(
    p_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data"
    SET "load_date" = "world"."mssql_getdate"()
    WHERE "char_id" = p_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setuserunloadtime"(
    p_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data"
    SET "unload_date" = "world"."mssql_getdate"()
    WHERE "char_id" = p_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_charlogout"(
    p_char_id integer,
    p_used_time_sec integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data"
    SET "logout" = "world"."mssql_getdate"(),
        "use_time" = "use_time" + p_used_time_sec
    WHERE "char_id" = p_char_id;
$function$;

-- Current slot ABI order (after p_char_id): 19 legacy equipment fields,
-- five agathions, artifact book, A[12], B[3], C[3], D[3], deco[6], waist,
-- brooch and jewel[6].
CREATE OR REPLACE FUNCTION "world"."lin_savecharacterslot"(
    p_char_id integer,
    p_st_underwear integer,
    p_st_right_ear integer,
    p_st_left_ear integer,
    p_st_neck integer,
    p_st_right_finger integer,
    p_st_left_finger integer,
    p_st_head integer,
    p_st_right_hand integer,
    p_st_left_hand integer,
    p_st_gloves integer,
    p_st_chest integer,
    p_st_legs integer,
    p_st_feet integer,
    p_st_back integer,
    p_st_both_hand integer,
    p_st_hair integer,
    p_st_hair2 integer,
    p_st_right_bracelet integer,
    p_st_left_bracelet integer,
    p_st_agathion_main integer,
    p_st_agathion_sub1 integer,
    p_st_agathion_sub2 integer,
    p_st_agathion_sub3 integer,
    p_st_agathion_sub4 integer,
    p_st_artifactbook integer,
    p_st_artifact_a1 integer,
    p_st_artifact_a2 integer,
    p_st_artifact_a3 integer,
    p_st_artifact_a4 integer,
    p_st_artifact_a5 integer,
    p_st_artifact_a6 integer,
    p_st_artifact_a7 integer,
    p_st_artifact_a8 integer,
    p_st_artifact_a9 integer,
    p_st_artifact_a10 integer,
    p_st_artifact_a11 integer,
    p_st_artifact_a12 integer,
    p_st_artifact_b1 integer,
    p_st_artifact_b2 integer,
    p_st_artifact_b3 integer,
    p_st_artifact_c1 integer,
    p_st_artifact_c2 integer,
    p_st_artifact_c3 integer,
    p_st_artifact_d1 integer,
    p_st_artifact_d2 integer,
    p_st_artifact_d3 integer,
    p_st_deco1 integer,
    p_st_deco2 integer,
    p_st_deco3 integer,
    p_st_deco4 integer,
    p_st_deco5 integer,
    p_st_deco6 integer,
    p_st_waist integer,
    p_st_brooch integer,
    p_st_jewel1 integer,
    p_st_jewel2 integer,
    p_st_jewel3 integer,
    p_st_jewel4 integer,
    p_st_jewel5 integer,
    p_st_jewel6 integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_slot"
    SET "st_underwear" = p_st_underwear,
        "st_right_ear" = p_st_right_ear,
        "st_left_ear" = p_st_left_ear,
        "st_neck" = p_st_neck,
        "st_right_finger" = p_st_right_finger,
        "st_left_finger" = p_st_left_finger,
        "st_head" = p_st_head,
        "st_right_hand" = p_st_right_hand,
        "st_left_hand" = p_st_left_hand,
        "st_gloves" = p_st_gloves,
        "st_chest" = p_st_chest,
        "st_legs" = p_st_legs,
        "st_feet" = p_st_feet,
        "st_back" = p_st_back,
        "st_both_hand" = p_st_both_hand,
        "st_hair" = p_st_hair,
        "st_hair2" = p_st_hair2,
        "st_right_bracelet" = p_st_right_bracelet,
        "st_left_bracelet" = p_st_left_bracelet,
        "st_agathion_main" = p_st_agathion_main,
        "st_agathion_sub1" = p_st_agathion_sub1,
        "st_agathion_sub2" = p_st_agathion_sub2,
        "st_agathion_sub3" = p_st_agathion_sub3,
        "st_agathion_sub4" = p_st_agathion_sub4,
        "st_artifactbook" = p_st_artifactbook,
        "st_artifact_a1" = p_st_artifact_a1,
        "st_artifact_a2" = p_st_artifact_a2,
        "st_artifact_a3" = p_st_artifact_a3,
        "st_artifact_a4" = p_st_artifact_a4,
        "st_artifact_a5" = p_st_artifact_a5,
        "st_artifact_a6" = p_st_artifact_a6,
        "st_artifact_a7" = p_st_artifact_a7,
        "st_artifact_a8" = p_st_artifact_a8,
        "st_artifact_a9" = p_st_artifact_a9,
        "st_artifact_a10" = p_st_artifact_a10,
        "st_artifact_a11" = p_st_artifact_a11,
        "st_artifact_a12" = p_st_artifact_a12,
        "st_artifact_b1" = p_st_artifact_b1,
        "st_artifact_b2" = p_st_artifact_b2,
        "st_artifact_b3" = p_st_artifact_b3,
        "st_artifact_c1" = p_st_artifact_c1,
        "st_artifact_c2" = p_st_artifact_c2,
        "st_artifact_c3" = p_st_artifact_c3,
        "st_artifact_d1" = p_st_artifact_d1,
        "st_artifact_d2" = p_st_artifact_d2,
        "st_artifact_d3" = p_st_artifact_d3,
        "st_deco1" = p_st_deco1,
        "st_deco2" = p_st_deco2,
        "st_deco3" = p_st_deco3,
        "st_deco4" = p_st_deco4,
        "st_deco5" = p_st_deco5,
        "st_deco6" = p_st_deco6,
        "st_waist" = p_st_waist,
        "st_brooch" = p_st_brooch,
        "st_jewel1" = p_st_jewel1,
        "st_jewel2" = p_st_jewel2,
        "st_jewel3" = p_st_jewel3,
        "st_jewel4" = p_st_jewel4,
        "st_jewel5" = p_st_jewel5,
        "st_jewel6" = p_st_jewel6
    WHERE "char_id" = p_char_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."user_slot" (
            "char_id",
            "st_underwear", "st_right_ear", "st_left_ear", "st_neck",
            "st_right_finger", "st_left_finger", "st_head",
            "st_right_hand", "st_left_hand", "st_gloves", "st_chest",
            "st_legs", "st_feet", "st_back", "st_both_hand", "st_hair",
            "st_hair2", "st_right_bracelet", "st_left_bracelet",
            "st_agathion_main", "st_agathion_sub1", "st_agathion_sub2",
            "st_agathion_sub3", "st_agathion_sub4", "st_artifactbook",
            "st_artifact_a1", "st_artifact_a2", "st_artifact_a3",
            "st_artifact_a4", "st_artifact_a5", "st_artifact_a6",
            "st_artifact_a7", "st_artifact_a8", "st_artifact_a9",
            "st_artifact_a10", "st_artifact_a11", "st_artifact_a12",
            "st_artifact_b1", "st_artifact_b2", "st_artifact_b3",
            "st_artifact_c1", "st_artifact_c2", "st_artifact_c3",
            "st_artifact_d1", "st_artifact_d2", "st_artifact_d3",
            "st_deco1", "st_deco2", "st_deco3", "st_deco4",
            "st_deco5", "st_deco6", "st_waist", "st_brooch",
            "st_jewel1", "st_jewel2", "st_jewel3", "st_jewel4",
            "st_jewel5", "st_jewel6"
        ) VALUES (
            p_char_id,
            p_st_underwear, p_st_right_ear, p_st_left_ear, p_st_neck,
            p_st_right_finger, p_st_left_finger, p_st_head,
            p_st_right_hand, p_st_left_hand, p_st_gloves, p_st_chest,
            p_st_legs, p_st_feet, p_st_back, p_st_both_hand, p_st_hair,
            p_st_hair2, p_st_right_bracelet, p_st_left_bracelet,
            p_st_agathion_main, p_st_agathion_sub1, p_st_agathion_sub2,
            p_st_agathion_sub3, p_st_agathion_sub4, p_st_artifactbook,
            p_st_artifact_a1, p_st_artifact_a2, p_st_artifact_a3,
            p_st_artifact_a4, p_st_artifact_a5, p_st_artifact_a6,
            p_st_artifact_a7, p_st_artifact_a8, p_st_artifact_a9,
            p_st_artifact_a10, p_st_artifact_a11, p_st_artifact_a12,
            p_st_artifact_b1, p_st_artifact_b2, p_st_artifact_b3,
            p_st_artifact_c1, p_st_artifact_c2, p_st_artifact_c3,
            p_st_artifact_d1, p_st_artifact_d2, p_st_artifact_d3,
            p_st_deco1, p_st_deco2, p_st_deco3, p_st_deco4,
            p_st_deco5, p_st_deco6, p_st_waist, p_st_brooch,
            p_st_jewel1, p_st_jewel2, p_st_jewel3, p_st_jewel4,
            p_st_jewel5, p_st_jewel6
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getchecksum"(
    p_owner integer,
    p_warehouse integer
) RETURNS TABLE (checksum bigint)
LANGUAGE sql
STABLE
AS $function$
    SELECT COALESCE(
        SUM(
            item."item_id"::bigint + item."item_type"::bigint +
            item."amount" + item."enchant"::bigint
        )::bigint,
        0::bigint
    ) AS checksum
    FROM "world"."user_item" AS item
    WHERE item."char_id" = p_owner
      AND item."warehouse" = p_warehouse;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setchecksum"(
    p_owner integer,
    p_warehouse integer,
    p_key text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_checksum bigint;
    v_encrypted bytea;
BEGIN
    SELECT SUM(
        item."item_id"::bigint + item."item_type"::bigint +
        item."amount" + item."enchant"::bigint
    )::bigint
    INTO v_checksum
    FROM "world"."user_item" AS item
    WHERE item."char_id" = p_owner
      AND item."warehouse" = p_warehouse;

    -- The shipping SQL returns without creating an item_check row when the
    -- warehouse contains no items.
    IF v_checksum IS NULL THEN
        RETURN;
    END IF;

    -- Platform boundary: pgcrypto replaces MSSQL EncryptByPassPhrase.  The
    -- value and key meaning are kept. Ciphertext bytes are not.
    v_encrypted := pgp_sym_encrypt(
        v_checksum::text,
        p_key,
        'cipher-algo=aes256, compress-algo=0'
    );

    UPDATE "world"."item_check"
    SET "checksum" = v_encrypted
    WHERE "owner" = p_owner
      AND "warehouse" = p_warehouse;

    IF NOT FOUND THEN
        INSERT INTO "world"."item_check" ("owner", "warehouse", "checksum")
        VALUES (p_owner, p_warehouse, v_encrypted);
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_itemcheck"(
    p_owner integer,
    p_warehouse integer,
    p_key text
) RETURNS TABLE (matches integer)
LANGUAGE plpgsql
STABLE
AS $function$
DECLARE
    v_actual bigint;
    v_stored bigint;
BEGIN
    SELECT COALESCE(
        SUM(
            item."item_id"::bigint + item."item_type"::bigint +
            item."amount" + item."enchant"::bigint
        )::bigint,
        0::bigint
    )
    INTO v_actual
    FROM "world"."user_item" AS item
    WHERE item."char_id" = p_owner
      AND item."warehouse" = p_warehouse;

    BEGIN
        SELECT pgp_sym_decrypt(check_row."checksum", p_key)::bigint
        INTO v_stored
        FROM "world"."item_check" AS check_row
        WHERE check_row."owner" = p_owner
          AND check_row."warehouse" = p_warehouse;
    EXCEPTION
        WHEN external_routine_invocation_exception OR data_exception THEN
            -- MSSQL DecryptByPassPhrase yields NULL for a wrong key or an
            -- invalid payload. Pgcrypto reports those cases as exceptions.
            v_stored := NULL;
    END;

    v_stored := COALESCE(v_stored, 0::bigint);
    RETURN QUERY SELECT CASE WHEN v_actual = v_stored THEN 1 ELSE 0 END;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setchecksumafterserverdown"(
    p_key text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    -- Character inventory/private warehouse (0, 1).
    UPDATE "world"."item_check" AS check_row
    SET "checksum" = pgp_sym_encrypt(
        calculated.checksum::text,
        p_key,
        'cipher-algo=aes256, compress-algo=0'
    )
    FROM (
        SELECT item."char_id", item."warehouse",
               SUM(
                   item."item_id"::bigint + item."item_type"::bigint +
                   item."amount" + item."enchant"::bigint
               )::bigint AS checksum
        FROM "world"."user_item" AS item
        WHERE item."char_id" IN (
            SELECT user_row."char_id"
            FROM "world"."user_data" AS user_row
            WHERE user_row."load_date" > user_row."unload_date"
        )
          AND item."warehouse" IN (0, 1)
        GROUP BY item."char_id", item."warehouse"
    ) AS calculated
    WHERE check_row."owner" = calculated."char_id"
      AND check_row."warehouse" = calculated."warehouse";

    -- Pledge warehouse (2).
    UPDATE "world"."item_check" AS check_row
    SET "checksum" = pgp_sym_encrypt(
        calculated.checksum::text,
        p_key,
        'cipher-algo=aes256, compress-algo=0'
    )
    FROM (
        SELECT item."char_id", item."warehouse",
               SUM(
                   item."item_id"::bigint + item."item_type"::bigint +
                   item."amount" + item."enchant"::bigint
               )::bigint AS checksum
        FROM "world"."user_item" AS item
        WHERE item."char_id" IN (
            SELECT pledge_row."pledge_id"
            FROM "world"."pledge" AS pledge_row
            WHERE pledge_row."load_date" > pledge_row."unload_date"
        )
          AND item."warehouse" = 2
        GROUP BY item."char_id", item."warehouse"
    ) AS calculated
    WHERE check_row."owner" = calculated."char_id"
      AND check_row."warehouse" = calculated."warehouse";

    -- Castle warehouse (3).
    UPDATE "world"."item_check" AS check_row
    SET "checksum" = pgp_sym_encrypt(
        calculated.checksum::text,
        p_key,
        'cipher-algo=aes256, compress-algo=0'
    )
    FROM (
        SELECT item."char_id", item."warehouse",
               SUM(
                   item."item_id"::bigint + item."item_type"::bigint +
                   item."amount" + item."enchant"::bigint
               )::bigint AS checksum
        FROM "world"."user_item" AS item
        WHERE item."char_id" IN (
            SELECT castle_row."id"
            FROM "world"."castle" AS castle_row
            WHERE castle_row."load_date" > castle_row."unload_date"
        )
          AND item."warehouse" = 3
        GROUP BY item."char_id", item."warehouse"
    ) AS calculated
    WHERE check_row."owner" = calculated."char_id"
      AND check_row."warehouse" = calculated."warehouse";
END;
$function$;

COMMIT;
