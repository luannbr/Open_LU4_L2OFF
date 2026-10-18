-- Account item procedures. The procedure signatures and the hlocked and
-- use_count fields match Cached.exe. The bodies come from the protocol 287
-- database, with changes for PostgreSQL and the current server.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

-- Current Cached.exe reads/writes/resets ensoul rows through three exact
-- procedure ABIs.  The relation itself is pinned modified-287 DDL. No stored
-- procedure body survived in the available exports.
CREATE TABLE "world"."user_item_ensoul" (
    "item_id" integer NOT NULL,
    "ensoul_slot_type" integer NOT NULL,
    "ensoul_slot_index" integer NOT NULL,
    "ensoul_option_id" integer NOT NULL,
    CONSTRAINT "pk_user_item_ensoul" PRIMARY KEY (
        "item_id", "ensoul_slot_type", "ensoul_slot_index", "ensoul_option_id"
    )
);

-- The broad donor archive has 15 columns. Current lin_CopyItemByDeletedChar
-- has two additional SMALLINT inputs and current lin_RestoreChar2 returns the
-- matching two values. A phased alteration also admits populated legacy
-- donor imports without inventing persistent DEFAULT constraints.
ALTER TABLE "world"."user_item_deleted"
    ADD COLUMN "hlocked" smallint;
ALTER TABLE "world"."user_item_deleted"
    ADD COLUMN "use_count" smallint;
UPDATE "world"."user_item_deleted"
SET "hlocked" = COALESCE("hlocked", 0),
    "use_count" = COALESCE("use_count", 0);
ALTER TABLE "world"."user_item_deleted"
    ALTER COLUMN "hlocked" SET NOT NULL,
    ALTER COLUMN "use_count" SET NOT NULL;

-- Exact zero-input current ABI. The donor's character IN/GROUP BY is written
-- as a correlated MAX over the same MSSQL-padded name equivalence class.
-- PostgreSQL's native IN/GROUP BY would otherwise split trailing-space names.
CREATE OR REPLACE FUNCTION "world"."lin_clearupcharfromaddedservice3changename"()
RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_data" AS character
    WHERE character.account_id = -4
      AND character.create_date <= "world"."mssql_getdate"() - INTERVAL '365 days'
      AND (
          SELECT MAX(history.log_date)
          FROM "world"."user_history" AS history
          WHERE history.log_action = 103
            AND "world"."mssql_text_eq"(
                    history.char_name,
                    character.char_name
                )
      ) <= "world"."mssql_getdate"() - INTERVAL '365 days';
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getcharbyaccountid"(
    p_account_id integer
) RETURNS TABLE (
    char_id integer,
    account_id integer,
    char_name varchar(50),
    account_name varchar(50)
)
LANGUAGE sql
STABLE
AS $function$
    SELECT character.char_id, character.account_id,
           character.char_name, character.account_name
    FROM "world"."user_data" AS character
    WHERE p_account_id > 0 AND character.account_id = p_account_id;
$function$;

-- The current caller binds one integer result but never Fetches it. The donor
-- emits no result row, so this table function completes empty.
CREATE OR REPLACE FUNCTION "world"."lin_deletecharclearpledge"(
    p_char_id integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_pledge_id integer;
    v_ruler_id integer;
    v_now_war_id integer;
    v_challenger integer;
    v_challengee integer;
BEGIN
    SELECT character.pledge_id INTO v_pledge_id
    FROM "world"."user_data" AS character
    WHERE character.char_id = p_char_id;

    IF COALESCE(v_pledge_id, 0) <> 0 THEN
        SELECT clan.ruler_id, clan.now_war_id
        INTO v_ruler_id, v_now_war_id
        FROM "world"."pledge" AS clan
        WHERE clan.pledge_id = v_pledge_id;

        IF v_ruler_id = p_char_id THEN
            IF COALESCE(v_now_war_id, 0) <> 0 THEN
                SELECT war.challenger, war.challengee
                INTO v_challenger, v_challengee
                FROM "world"."pledge_war" AS war
                WHERE war.id = v_now_war_id;

                UPDATE "world"."pledge" AS clan
                SET now_war_id = 0
                WHERE clan.pledge_id IN (v_challenger, v_challengee);
                DELETE FROM "world"."pledge_war" AS war
                WHERE war.id = v_now_war_id;
            END IF;

            DELETE FROM "world"."academy_member" AS member
            WHERE member.pledge_id = v_pledge_id;
            DELETE FROM "world"."academy" AS academy
            WHERE academy.pledge_id = v_pledge_id;
            DELETE FROM "world"."sub_pledge" AS sub
            WHERE sub.pledge_id = v_pledge_id;
            DELETE FROM "world"."pledge_skill" AS skill
            WHERE skill.pledge_id = v_pledge_id;
            DELETE FROM "world"."pledge" AS clan
            WHERE clan.pledge_id = v_pledge_id;
            UPDATE "world"."user_data" AS character
            SET pledge_id = 0, pledge_type = 0, grade_id = 0
            WHERE character.pledge_id = v_pledge_id;
        END IF;

        UPDATE "world"."user_data" AS character
        SET pledge_id = 0
        WHERE character.char_id = p_char_id;
    END IF;

    RETURN;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletechar"(
    p_char_id integer,
    p_select_deleted_item integer DEFAULT 1
) RETURNS TABLE (item_id integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_original_account_id integer := 0;
    v_original_char_name varchar(50);
    v_original_account_name varchar(50);
    v_create_date timestamp without time zone;
    v_backup_char_name varchar(50);
BEGIN
    SELECT character.account_id, character.char_name,
           character.account_name, character.create_date
    INTO v_original_account_id, v_original_char_name,
         v_original_account_name, v_create_date
    FROM "world"."user_data" AS character
    WHERE character.char_id = p_char_id;

    v_backup_char_name := LEFT(
        v_original_char_name || '_' || v_original_account_id::text || '_' || p_char_id::text,
        50
    );

    UPDATE "world"."user_data" AS character
    SET account_id = -1, char_name = v_backup_char_name, pledge_id = 0
    WHERE character.char_id = p_char_id;

    INSERT INTO "world"."user_deleted" (char_id, delete_date)
    VALUES (p_char_id, "world"."mssql_getdate"());

    IF p_select_deleted_item = 1 THEN
        RETURN QUERY
        SELECT item.item_id
        FROM "world"."user_item" AS item
        WHERE item.char_id = p_char_id
          AND item.warehouse IN (0, 1, 1001);
    END IF;

    IF v_original_account_id <> -1 THEN
        INSERT INTO "world"."user_history" (
            char_name, char_id, log_date, log_action, account_name, create_date
        ) VALUES (
            v_original_char_name, p_char_id, "world"."mssql_getdate"(), 2,
            v_original_account_name, v_create_date
        );
    END IF;

    -- Atomic PostgreSQL replacement for donor existence-check/update-or-insert.
    INSERT INTO "world"."user_data_extra" (char_id, modified_date)
    VALUES (p_char_id, "world"."mssql_getdate"())
    ON CONFLICT (char_id) DO UPDATE
    SET modified_date = EXCLUDED.modified_date;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletenotowneditem"()
RETURNS TABLE (
    item_id integer,
    char_id integer,
    item_type integer,
    warehouse integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    stale record;
BEGIN
    CREATE TEMP TABLE IF NOT EXISTS cached_account_item_orphans (
        item_id integer,
        char_id integer,
        item_type integer,
        warehouse integer
    ) ON COMMIT DROP;
    TRUNCATE TABLE pg_temp.cached_account_item_orphans;

    INSERT INTO pg_temp.cached_account_item_orphans
    SELECT item.item_id, item.char_id, item.item_type, item.warehouse
    FROM "world"."user_item" AS item
    WHERE item.char_id = 0 OR item.item_type = 0 OR item.amount = 0;

    DELETE FROM "world"."user_item" AS item
    WHERE item.item_id IN (
        SELECT orphan.item_id FROM pg_temp.cached_account_item_orphans AS orphan
    );

    FOR stale IN
        SELECT character.char_id
        FROM "world"."user_data" AS character
        WHERE character.account_id > 0
          AND character.temp_delete_date IS NOT NULL
          AND character.temp_delete_date <= "world"."mssql_getdate"() - INTERVAL '7 days'
    LOOP
        PERFORM * FROM "world"."lin_deletechar"(stale.char_id, 1);
    END LOOP;

    PERFORM "world"."lin_cleanupghostdata"();

    RETURN QUERY
    SELECT orphan.item_id, orphan.char_id, orphan.item_type, orphan.warehouse
    FROM pg_temp.cached_account_item_orphans AS orphan;
END;
$function$;

-- Result numbers 1..22 keep the donor projection. Current CItem::Load
-- binds numbers 23/24 to the fields at +0x50/+0x100: hlocked/use_count.
CREATE OR REPLACE FUNCTION "world"."lin_loaditembyitemid"(
    p_item_id integer
) RETURNS TABLE (
    char_id integer,
    item_type integer,
    amount bigint,
    enchant integer,
    eroded integer,
    bless integer,
    ident integer,
    wished integer,
    warehouse integer,
    variation_opt1 integer,
    variation_opt2 integer,
    intensive_item_type integer,
    inventory_slot_index integer,
    appearance_type integer,
    attack_attribute_type smallint,
    attack_attribute_value smallint,
    defend_attribute_0 smallint,
    defend_attribute_1 smallint,
    defend_attribute_2 smallint,
    defend_attribute_3 smallint,
    defend_attribute_4 smallint,
    defend_attribute_5 smallint,
    hlocked smallint,
    use_count smallint
)
LANGUAGE sql
STABLE
AS $function$
    SELECT item.char_id, item.item_type, item.amount, item.enchant,
           item.eroded, item.bless::integer, item.ident,
           item.wished::integer, item.warehouse,
           COALESCE(item.variation_opt1, 0),
           COALESCE(item.variation_opt2, 0),
           COALESCE(item.intensive_item_type, 0),
           COALESCE(item.inventory_slot_index, -1),
           COALESCE(appearance.apperance_type, 0),
           COALESCE(attribute.attack_attribute_type, -2)::smallint,
           COALESCE(attribute.attack_attribute_value, 0)::smallint,
           COALESCE(attribute.defend_attribute_0, 0)::smallint,
           COALESCE(attribute.defend_attribute_1, 0)::smallint,
           COALESCE(attribute.defend_attribute_2, 0)::smallint,
           COALESCE(attribute.defend_attribute_3, 0)::smallint,
           COALESCE(attribute.defend_attribute_4, 0)::smallint,
           COALESCE(attribute.defend_attribute_5, 0)::smallint,
           item.hlocked, item.use_count
    FROM "world"."user_item" AS item
    LEFT JOIN "world"."user_item_attribute" AS attribute
      ON attribute.item_id = item.item_id
    LEFT JOIN "world"."user_item_apperance" AS appearance
      ON appearance.item_id = item.item_id
    WHERE item.item_id = p_item_id AND item.item_type > 0;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_copyitembydeletedchar"(
    p_char_id integer,
    p_item_type integer,
    p_amount bigint,
    p_enchant integer,
    p_eroded integer,
    p_bless integer,
    p_ident integer,
    p_wished integer,
    p_warehouse integer,
    p_item_id integer,
    p_variation_opt1 integer,
    p_variation_opt2 integer,
    p_intensive_item_type integer,
    p_inventory_slot_index integer,
    p_hlocked smallint,
    p_use_count smallint
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_bless < 0 OR p_bless > 255 THEN
        p_bless := 0;
    END IF;

    INSERT INTO "world"."user_item_deleted" (
        item_id, char_id, item_type, amount, enchant, eroded, bless,
        ident, wished, warehouse, variation_opt1, variation_opt2,
        intensive_item_type, inventory_slot_index, hlocked, use_count
    ) VALUES (
        p_item_id, p_char_id, p_item_type, p_amount, p_enchant, p_eroded,
        p_bless, p_ident, p_wished, p_warehouse, p_variation_opt1,
        p_variation_opt2, p_intensive_item_type, p_inventory_slot_index,
        p_hlocked, p_use_count
    );
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateuseritem"(
    p_char_id integer,
    p_item_type integer,
    p_amount bigint,
    p_enchant integer,
    p_eroded integer,
    p_bless integer,
    p_ident integer,
    p_wished integer,
    p_warehouse integer,
    p_item_id integer,
    p_variation_opt1 integer,
    p_variation_opt2 integer,
    p_intensive_item_type integer,
    p_inventory_slot_index integer,
    p_hlocked smallint,
    p_use_count smallint
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_bless < 0 OR p_bless > 255 THEN
        p_bless := 0;
    END IF;

    UPDATE "world"."user_item" AS item
    SET char_id = p_char_id,
        item_type = p_item_type,
        amount = p_amount,
        enchant = p_enchant,
        eroded = p_eroded,
        bless = p_bless,
        ident = p_ident,
        wished = p_wished,
        warehouse = p_warehouse,
        variation_opt1 = p_variation_opt1,
        variation_opt2 = p_variation_opt2,
        intensive_item_type = p_intensive_item_type,
        inventory_slot_index = p_inventory_slot_index,
        hlocked = p_hlocked,
        use_count = p_use_count
    WHERE item.item_id = p_item_id;
END;
$function$;

-- Exact current callable shapes over the pinned ensoul relation.  The effects
-- below are inferred from CItem::LoadEnsoulInfo, CItem::SetEnsoulInfo and
-- EtcManiEnsoul::Rollback. They are not presented as recovered external T-SQL
-- bodies.  Set is idempotent because current CItem::Copy (004ebabc) calls it
-- with the source item's unchanged database id.  The same-key early return is
-- also present in the available same-schema SQL Server replica insert path.
CREATE OR REPLACE FUNCTION "world"."lin_getitemensoulinfo"(
    p_item_id integer
) RETURNS TABLE (
    ensoul_slot_type integer,
    ensoul_slot_index integer,
    ensoul_option_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT ensoul.ensoul_slot_type, ensoul.ensoul_slot_index,
           ensoul.ensoul_option_id
    FROM "world"."user_item_ensoul" AS ensoul
    WHERE ensoul.item_id = p_item_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setitemensoulinfo"(
    p_item_id integer,
    p_slot_type integer,
    p_slot_index integer,
    p_option_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_item_ensoul" (
        item_id, ensoul_slot_type, ensoul_slot_index, ensoul_option_id
    ) VALUES (p_item_id, p_slot_type, p_slot_index, p_option_id)
    ON CONFLICT (
        item_id, ensoul_slot_type, ensoul_slot_index, ensoul_option_id
    ) DO NOTHING;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_resetitemensoulinfo"(
    p_item_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_item_ensoul" AS ensoul
    WHERE ensoul.item_id = p_item_id;
$function$;

-- Exact current nine-input ABI and pinned modified-287 update-or-insert body.
CREATE OR REPLACE FUNCTION "world"."lin_updateuseritemattribute"(
    p_item_id integer,
    p_attack_attribute_type smallint,
    p_attack_attribute_value smallint,
    p_defend_attribute_0 smallint,
    p_defend_attribute_1 smallint,
    p_defend_attribute_2 smallint,
    p_defend_attribute_3 smallint,
    p_defend_attribute_4 smallint,
    p_defend_attribute_5 smallint
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_item_attribute" (
        item_id, attack_attribute_type, attack_attribute_value,
        defend_attribute_0, defend_attribute_1, defend_attribute_2,
        defend_attribute_3, defend_attribute_4, defend_attribute_5
    ) VALUES (
        p_item_id, p_attack_attribute_type, p_attack_attribute_value,
        p_defend_attribute_0, p_defend_attribute_1, p_defend_attribute_2,
        p_defend_attribute_3, p_defend_attribute_4, p_defend_attribute_5
    )
    ON CONFLICT (item_id) DO UPDATE SET
        attack_attribute_type = EXCLUDED.attack_attribute_type,
        attack_attribute_value = EXCLUDED.attack_attribute_value,
        defend_attribute_0 = EXCLUDED.defend_attribute_0,
        defend_attribute_1 = EXCLUDED.defend_attribute_1,
        defend_attribute_2 = EXCLUDED.defend_attribute_2,
        defend_attribute_3 = EXCLUDED.defend_attribute_3,
        defend_attribute_4 = EXCLUDED.defend_attribute_4,
        defend_attribute_5 = EXCLUDED.defend_attribute_5;
$function$;

-- All six current call sites pass literal NULL as the second argument. The
-- recovered donor has one argument and proves the five deletion effects below.
-- The meaning of a non-NULL second argument remains unresolved.  Ensoul
-- cleanup is added from the current relation ownership and Reset ABI, not from
-- the unavailable current lin_DeleteItem body.
CREATE OR REPLACE FUNCTION "world"."lin_deleteitem"(
    p_item_id integer,
    p_current_unresolved integer DEFAULT NULL
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_deleted integer;
BEGIN
    DELETE FROM "world"."user_item_ensoul" AS ensoul
    WHERE ensoul.item_id = p_item_id;
    DELETE FROM "world"."user_item_duration" AS duration
    WHERE duration.item_id = p_item_id;
    DELETE FROM "world"."user_item_attribute" AS attribute
    WHERE attribute.item_id = p_item_id;
    DELETE FROM "world"."user_item_period" AS period
    WHERE period.item_id = p_item_id;
    DELETE FROM "world"."user_item_apperance" AS appearance
    WHERE appearance.item_id = p_item_id;
    DELETE FROM "world"."user_item" AS item
    WHERE item.item_id = p_item_id;
    GET DIAGNOSTICS v_deleted = ROW_COUNT;
    RETURN QUERY SELECT v_deleted;
END;
$function$;

COMMIT;
